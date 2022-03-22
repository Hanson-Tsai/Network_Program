#include <iostream>
#include <string.h>
#include <vector>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <regex>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
using namespace std;
//ssh hstsai@nplinux9.cs.nctu.edu.tw

#define MAXUSER 30

//Struct ------------------------------------------------------------------------
typedef struct cmdBlock{
	string cmd;
	vector<string> argv;
	bool read_pipe, write_pipe;
	int pipeType; //0: "|",   1: "!",   2: ">",   3: "no pipe",  4: "<"
    int num;
    bool skip;
    bool HasSpace; // true:"file redirect"   false:"user pipe"
}cmdBlock;

typedef struct Pipe{
	int count;
	int fd[2];
}Pipe;

typedef struct envVariable{
	string name;
	string value;
}envVariable;

typedef struct User{
	bool has_user;

	int slave_sock;
	string client_address;

	bool done_serving;
	int ID;
	string name;
	vector<Pipe> pipes;
    vector<Pipe> numpipes;
    cmdBlock next_cmdBlock;
	vector<envVariable> envVariables;
}User;

typedef struct User_pipe{
	int senderID;
	int receiverID;
	int fd[2];
}User_pipe;


//declare function calls -------------------------------------------------------

void initiate_userdata(int index);

int Creating_socket(unsigned short port);

int Accepting_new_user(int master_sock);

int find_Max_Num(int master_sock);

void welcome_msg(int ID);

void login_msg(int ID);

void logout_msg(int ID);

void who();

void tell(int targetID, string Msg);

void yell(string Msg);

void name(string newName);

void broadcasting(int *sourceID, int *targetID, string Msg);

bool check_UserPipe_Exist(int &index, int senderID, int receiverID);

void handle_UserPipe(Pipe &newPipe, int senderID, int receiverID, bool type);

string getClientName(string Client_Name);

void Run_shell(int ID);

void cmd_packing(string cmdLine ,vector<cmdBlock> &cmdBlocks);

bool cmd_exe(cmdBlock &cmdBlock);

void string2arr(string cmd, vector<string> &argv, char *arr[]);

void build_pipe(cmdBlock &cmdBlock);

void what_is_next(string cmd, vector<string> &argv);

int find_number();


// Regex ------------------------------------------------------------------------
static regex reg("[0-9]+");


// Global variable --------------------------------------------------------------
fd_set Active_FDs, Read_FDs;
User Clients[MAXUSER];
vector<User_pipe> User_pipes;
int ID_serv;
string now_cmdLine;


// Function ---------------------------------------------------------------------
int main(int argc, char *argv[]){
	if (argc != 2){
		return 0;
	}

    for (int i=0; i<MAXUSER; i++){
		initiate_userdata(i);
	}
    FD_ZERO(&Active_FDs);
	FD_ZERO(&Read_FDs);
	ID_serv = 0;
    User_pipes.clear();
	clearenv();

	unsigned short port = (unsigned short)atoi(argv[1]);

	int master_sock = Creating_socket(port);
	listen(master_sock, MAXUSER);

    struct timeval time_val = {0, 10};
	while(1){
		bcopy(&Active_FDs, &Read_FDs, sizeof(Read_FDs));
        
reselecting:
		if (select(find_Max_Num(master_sock), &Read_FDs, NULL, NULL, &time_val) < 0){
			cerr << "Fail to select" << endl;
            goto reselecting;
		}

		if (FD_ISSET(master_sock, &Read_FDs)){
			Accepting_new_user(master_sock);
		}

		for (int i=0; i<MAXUSER; i++){
			Run_shell(i);
		}
	}
	return 0;
}

void initiate_userdata(int index){
	Clients[index].has_user = false;
	Clients[index].slave_sock = 0;
	Clients[index].client_address = "";
	Clients[index].done_serving = true;
	Clients[index].ID = 0;
	Clients[index].name = "";
	Clients[index].envVariables.clear();
	envVariable new_envVariable;
	new_envVariable.name = "PATH";
	new_envVariable.value = "bin:.";
	Clients[index].envVariables.push_back(new_envVariable);
    //clear normal pipe
	for (int i=0; i<Clients[index].pipes.size(); i++){
		close(Clients[index].pipes[i].fd[0]);
		close(Clients[index].pipes[i].fd[1]);
	}
	Clients[index].pipes.clear();
    //clear num pipe
    for (int i=0; i<Clients[index].numpipes.size(); i++){
		close(Clients[index].numpipes[i].fd[0]);
		close(Clients[index].numpipes[i].fd[1]);
	}
    Clients[index].numpipes.clear();
    //clear user pipe
    for (int i=0; i<User_pipes.size(); i++){
		if (User_pipes[i].senderID == index || User_pipes[i].receiverID == index){
			close(User_pipes[i].fd[0]);
			close(User_pipes[i].fd[1]);
			User_pipes.erase(User_pipes.begin()+i);
		}
	}
    Clients[index].next_cmdBlock = {};
}

int Creating_socket(unsigned short port){
	int master_sock;
	if ((master_sock = socket(AF_INET, SOCK_STREAM, 0)) < 0){
		cerr << "Socket create fail.\n";
		exit(0);
	}
	struct sockaddr_in sin;

	bzero((char *)&sin, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = INADDR_ANY;
    sin.sin_port = htons(port);

	if (bind(master_sock, (struct sockaddr *)&sin, sizeof(sin)) < 0){
		cerr << "Socket bind fail.\n";
		exit(0);
	}
    FD_SET(master_sock, &Active_FDs);
	return master_sock;
}

int Accepting_new_user(int master_sock){
	struct sockaddr_in sin;
	unsigned int alen = sizeof(sin);
    bool state = false;

    for (int i=0; i<MAXUSER; i++){
		if (Clients[i].has_user){
			continue;
		} else {
			int slave_sock;
			if ((slave_sock = accept(master_sock, (struct sockaddr *)&sin, &alen)) < 0){
				return state;
			}

			Clients[i].has_user = true;
			Clients[i].slave_sock = slave_sock;
			Clients[i].client_address = inet_ntoa(sin.sin_addr);
			Clients[i].client_address += ":" + to_string(htons(sin.sin_port));
			Clients[i].ID = i;

			state = true;
			FD_SET(Clients[i].slave_sock, &Active_FDs);

			welcome_msg(Clients[i].ID);
			login_msg(Clients[i].ID);
			break;
		}
	}

	return state;
}

int find_Max_Num(int master_sock){
	int maxValue = master_sock;
	for (int i=0; i<MAXUSER; i++){
		if (Clients[i].slave_sock > maxValue){
			maxValue = Clients[i].slave_sock;
		}
	}
	return maxValue + 1;
}

void welcome_msg(int ID){
	string Msg = "";
	Msg = Msg + "****************************************\n"
			  + "** Welcome to the information server. **\n"
			  + "****************************************\n";
	broadcasting(NULL, &ID, Msg);
}

void login_msg(int ID){
	string Msg = "*** User '" + getClientName(Clients[ID].name) + "' entered from " + Clients[ID].client_address + ". ***\n";
	broadcasting(NULL, NULL, Msg);
}

void logout_msg(int ID){
	string Msg = "*** User '" + getClientName(Clients[ID].name) + "' left. ***\n";
	broadcasting(NULL, NULL, Msg);
}

void who(){
	string Msg = "<ID>\t<nickname>\t<IP:port>\t<indicate me>\n";
	for (int i=0; i<MAXUSER; i++){
		if (Clients[i].has_user){
			Msg += to_string(Clients[i].ID + 1) + "\t" + getClientName(Clients[i].name) + "\t" + Clients[i].client_address;
			if (Clients[i].ID == ID_serv){
				Msg += "\t<-me";
			}
			Msg += "\n";
		}
	}
	broadcasting(NULL, &ID_serv, Msg);
}

void tell(int targetID, string Msg){
	if (Clients[targetID].has_user){
		Msg = "*** " + getClientName(Clients[ID_serv].name) + " told you ***: " + Msg + "\n";
		broadcasting(NULL, &targetID, Msg);
	} else {
		Msg = "*** Error: user #" + to_string(targetID + 1) + " does not exist yet. ***\n";
		broadcasting(NULL, &ID_serv, Msg);
	}
}

void yell(string Msg){
	Msg = "*** " + getClientName(Clients[ID_serv].name) + " yelled ***: " + Msg + "\n";
	broadcasting(NULL, NULL, Msg);
}

void name(string newName){
	for (int i=0; i<MAXUSER; i++){
		if (i == ID_serv){
			continue;
		}
		if (Clients[i].has_user && Clients[i].name == newName){
			string Msg = "*** User '" + newName + "' already exists. ***\n";
			broadcasting(NULL, &ID_serv, Msg);
			return;
		}
	}
	Clients[ID_serv].name = newName;
	string Msg = "*** User from " + Clients[ID_serv].client_address + " is named '" + Clients[ID_serv].name + "'. ***\n";
	broadcasting(NULL, NULL, Msg);
}

void broadcasting(int *sourceID, int *targetID, string origin_Msg){
	const char *Msg = origin_Msg.c_str();
	char unit;
	if (targetID == NULL){
		for (int i=0; i<MAXUSER; i++){
			if (Clients[i].has_user){
				write(Clients[i].slave_sock, Msg, sizeof(unit)*origin_Msg.length());
			}
		}
	} else {
		write(Clients[*targetID].slave_sock, Msg, sizeof(unit)*origin_Msg.length());
	}
}

bool check_UserPipe_Exist(int &index, int senderID, int receiverID){
	bool Existed = false;
	for (int i=0; i<User_pipes.size(); i++){
		if (User_pipes[i].senderID == senderID && User_pipes[i].receiverID == receiverID){
			index = i;
			Existed = true;
			break;
		}
	}
	return Existed;
}

void handle_UserPipe(Pipe &newPipe, int senderID, int receiverID, bool type){
    int index;
    int FD_NULL = open("/dev/null", O_RDWR);
    //sending 
    if(type == 0){
        if (receiverID < 0 || receiverID  > 29 || !Clients[receiverID].has_user){
			string Msg = "*** Error: user #" + to_string(receiverID + 1) + " does not exist yet. ***\n";
			broadcasting(NULL, &senderID, Msg);
            newPipe.fd[1] = FD_NULL;
            newPipe.fd[0] = -2;
		} else {
			if (check_UserPipe_Exist(index, senderID, receiverID)){
				string Msg = "*** Error: the pipe #" + to_string(senderID + 1) + "->#" + to_string(receiverID + 1) + " already exists. ***\n";
				broadcasting(NULL, &senderID, Msg);
                newPipe.fd[1] = FD_NULL;
		        newPipe.fd[0] = -2;
			} else {
				string Msg = "*** " + getClientName(Clients[senderID].name) + " (#" + to_string(senderID + 1) + ") just piped '" + now_cmdLine + "' to "
							+ getClientName(Clients[receiverID].name) + " (#" + to_string(receiverID + 1) + ") ***\n";
				broadcasting(NULL, NULL, Msg);
				User_pipe newUserPipe;
				newUserPipe.senderID = senderID;
				newUserPipe.receiverID = receiverID;
				pipe(newUserPipe.fd);
				User_pipes.push_back(newUserPipe);
				newPipe.fd[1] = newUserPipe.fd[1];
				newPipe.fd[0] = -2;
			}
		}
    }
    //receiving
    if(type == 1){
        if (senderID < 0 || senderID > 29 || !Clients[senderID].has_user){
			string Msg = "*** Error: user #" + to_string(senderID + 1) + " does not exist yet. ***\n";
			broadcasting(NULL, &receiverID, Msg);
            newPipe.fd[0] = FD_NULL;
	        newPipe.fd[1] = -2;
		} else {
			if (check_UserPipe_Exist(index, senderID, receiverID)){
				string Msg = "*** " + getClientName(Clients[receiverID].name) + " (#" + to_string(receiverID + 1) + ") just received from "
							+ getClientName(Clients[senderID].name) + " (#" + to_string(senderID + 1) + ") by '" + now_cmdLine + "' ***\n";
				broadcasting(NULL, NULL, Msg);
				newPipe.fd[0] = User_pipes[index].fd[0];
				newPipe.fd[1] = -2;
			} else {
				string Msg = "*** Error: the pipe #" + to_string(senderID + 1) + "->#" + to_string(receiverID + 1) + " does not exist yet. ***\n";
				broadcasting(NULL, &receiverID, Msg);
                newPipe.fd[0] = FD_NULL;
		        newPipe.fd[1] = -2;
			}
		}
    }
}

string getClientName(string Client_Name){
	if (Client_Name == "") return "(no name)";
	else return Client_Name;
}

//-------------------------------------------------------------------------------
void Run_shell(int ID){
    ID_serv = ID;
    if (!Clients[ID_serv].has_user){
		return;
	}
	clearenv();
	for (int i=0; i<Clients[ID_serv].envVariables.size(); i++){
		setenv(Clients[ID_serv].envVariables[i].name.data(), Clients[ID_serv].envVariables[i].value.data(), 1);
	}

    string cmd_Line;
    char readbuffer[15000];
    vector<cmdBlock> cmdBlocks;

    if (Clients[ID_serv].done_serving){
		string Msg = "% ";
		broadcasting(NULL, &ID_serv, Msg);
		Clients[ID_serv].done_serving = false;
	}

    if (FD_ISSET(Clients[ID_serv].slave_sock, &Read_FDs)){
		bzero(readbuffer, sizeof(readbuffer));
		int readCount = read(Clients[ID_serv].slave_sock, readbuffer, sizeof(readbuffer));
		if (readCount < 0){
			cerr << ID_serv << ": read error." << endl;
			return;
		} else {
			cmd_Line = readbuffer;
			if (cmd_Line[cmd_Line.length()-1] == '\n'){
				cmd_Line = cmd_Line.substr(0, cmd_Line.length()-1);
				if (cmd_Line[cmd_Line.length()-1] == '\r'){
					cmd_Line = cmd_Line.substr(0, cmd_Line.length()-1);
				}
			}
		}
	} else {
		return;
	}

	if (cmd_Line.length() == 0){
		Clients[ID_serv].done_serving = true;
		return;
	}

    //serving ID:user's cmd
    now_cmdLine = cmd_Line;
    cmd_packing(cmd_Line, cmdBlocks);
    while(!cmdBlocks.empty()){

        if(cmdBlocks[0].pipeType != 3){
            Clients[ID_serv].next_cmdBlock = cmdBlocks[1]; //prefatch cmd to be global variable
        }
            
        if (!cmd_exe(cmdBlocks[0])){
			return;
		}
            
        if(cmdBlocks[0].skip == true){
            cmdBlocks.erase(cmdBlocks.begin());
        }
            
        cmdBlocks.erase(cmdBlocks.begin());
        Clients[ID_serv].next_cmdBlock = {};
    }

    // reduction of num pipe
    int n =  Clients[ID_serv].pipes.size(); 
    for(int i=0; i < n; i++){
        if(Clients[ID_serv].pipes.size() != 0){
            if(Clients[ID_serv].pipes[0].count > 0){
                Clients[ID_serv].numpipes.push_back(Clients[ID_serv].pipes[0]);
            }
            Clients[ID_serv].pipes.erase(Clients[ID_serv].pipes.begin());
        }
     }

    for(int i=0; i <= Clients[ID_serv].numpipes.size(); i++){
        if(Clients[ID_serv].numpipes.size() != 0){
            Clients[ID_serv].numpipes[i].count--;
        }
    }
    
    // ID:user round is finished
    Clients[ID_serv].done_serving = true;
}

void cmd_packing(string cmdLine ,vector<cmdBlock> &cmdBlocks){
    int last = 0;
    int start = 0;
    int index = 0;
    int num_of_cmd = 0;
    bool talkcmd = false;
    cmdBlock cmdBlock;
    string tmp;
    
    cmdLine += "&";

    //true if cmdLine is tell or yell
    if( (cmdLine[0] == 'y' || cmdLine[0] == 't') && cmdLine[1] == 'e' && cmdLine[2] == 'l' && cmdLine[3] == 'l' ){
        talkcmd = true;
    }

    do{
        //initial cmdBlock
        cmdBlock.read_pipe = false;
		cmdBlock.write_pipe = false;
		cmdBlock.pipeType = 0;
        cmdBlock.skip = false;
        cmdBlock.HasSpace = false;

        //spilt cmd
        if(cmdLine[index] == '|' && !talkcmd){
            num_of_cmd++;
            cmdBlock.num = num_of_cmd - 1;
            cmdBlock.cmd.assign(cmdLine, start, index-start);
            cmdBlock.pipeType = 0;
            cmdBlocks.push_back(cmdBlock);
            index++;
            start = index;
            
        }else if(cmdLine[index] == '!' && !talkcmd){
            num_of_cmd++;
            cmdBlock.num = num_of_cmd - 1;
            cmdBlock.cmd.assign(cmdLine, start, index-start);
            cmdBlock.pipeType = 1;
            cmdBlocks.push_back(cmdBlock);
            index++;
            start = index;
        }else if(cmdLine[index] == '>' && !talkcmd){
            num_of_cmd++;
            cmdBlock.num = num_of_cmd - 1;
            cmdBlock.cmd.assign(cmdLine, start, index-start);
            cmdBlock.pipeType = 2;
            cmdBlock.skip = true;
            if(cmdLine[index+1] == ' '){cmdBlock.HasSpace = true;}
            cmdBlocks.push_back(cmdBlock);
            index++;
            start = index;
        }else if(cmdLine[index] == '&'){
            num_of_cmd++;
            cmdBlock.num = num_of_cmd - 1;
            cmdBlock.cmd.assign(cmdLine, start, index-start);
            cmdBlock.pipeType = 3;
            if(index == 0){
                cmdBlock.cmd = "nothing_enter";
            }
            cmdBlocks.push_back(cmdBlock);
            last = 1;
        }else if(cmdLine[index] == '<' && !talkcmd){
            num_of_cmd++;
            cmdBlock.num = num_of_cmd - 1;
            cmdBlock.cmd.assign(cmdLine, start, index-start);
            cmdBlock.pipeType = 4;
            cmdBlock.skip = true;
            cmdBlocks.push_back(cmdBlock);
            index++;
            start = index;
        }else{
            index++;
        }
    }while(!last);
}

bool cmd_exe(cmdBlock &cmdBlock){
    int child_pid ,status;
    char *argv[1000];
    string cmd;

    string2arr(cmdBlock.cmd, cmdBlock.argv, argv);
    cmd = cmdBlock.argv[0].data();
    
    if (cmd == "printenv"){
        if(getenv(cmdBlock.argv[1].data()) != NULL){
            string Msg = getenv(cmdBlock.argv[1].data());
			broadcasting(NULL, &ID_serv, Msg + "\n");
        }
    } else if(cmd == "setenv"){
		envVariable new_envVariable;
		new_envVariable.name = cmdBlock.argv[1];
		new_envVariable.value = cmdBlock.argv[2];
		Clients[ID_serv].envVariables.push_back(new_envVariable);
		setenv(cmdBlock.argv[1].data(), cmdBlock.argv[2].data(), 1);
    } else if (cmd == "who"){
		who();
	} else if (cmd == "tell"){
		string Msg = cmdBlock.cmd.substr(cmdBlock.cmd.find(cmdBlock.argv[2]));
		tell(stoi(cmdBlock.argv[1])-1, Msg);
	} else if (cmd == "yell"){
		string Msg = cmdBlock.cmd.substr(cmdBlock.cmd.find(cmdBlock.argv[1]));
		yell(Msg);
	} else if (cmd == "name"){
		name(cmdBlock.argv[1]);
    } else if(cmd == "exit"){
        logout_msg(ID_serv);
		FD_CLR(Clients[ID_serv].slave_sock, &Active_FDs);
		close(Clients[ID_serv].slave_sock);
		initiate_userdata(ID_serv);
		while(waitpid(-1, &status, WNOHANG) > 0){}
		return false;
    } else if(cmd == "nothing_enter"){
        //do nothing
    } else{

        build_pipe(cmdBlock); //build pipe

        while((child_pid = fork()) < 0){
			while(waitpid(-1, &status, WNOHANG) > 0); //fork error
            exit(EXIT_FAILURE);
		}

        switch (child_pid)
        {
        case 0: //****child process****
            //number pipes to stdin
            if(cmdBlock.num == 0){
                for(int i=0; i <= Clients[ID_serv].numpipes.size(); i++){            
                    if(Clients[ID_serv].numpipes.size() == 0){ i++;}
                    else if(Clients[ID_serv].numpipes[i].count == 0){
                        close(Clients[ID_serv].numpipes[i].fd[1]);
                        dup2(Clients[ID_serv].numpipes[i].fd[0], STDIN_FILENO);
                        close(Clients[ID_serv].numpipes[i].fd[0]);
                    }
                }
            }
            // connect other pipe
            if(cmdBlock.read_pipe){
                if(cmdBlock.pipeType == 4){
                    close(Clients[ID_serv].pipes[cmdBlock.num].fd[1]);
                    dup2(Clients[ID_serv].pipes[cmdBlock.num].fd[0], STDIN_FILENO);
                    close(Clients[ID_serv].pipes[cmdBlock.num].fd[0]);
                }else{
                close(Clients[ID_serv].pipes[cmdBlock.num - 1].fd[1]);
                dup2(Clients[ID_serv].pipes[cmdBlock.num - 1].fd[0], STDIN_FILENO);
                close(Clients[ID_serv].pipes[cmdBlock.num - 1].fd[0]);
                }
            }
            if(cmdBlock.write_pipe){
                close(Clients[ID_serv].pipes[cmdBlock.num].fd[0]);
                if(cmdBlock.pipeType == 1){
                    dup2(Clients[ID_serv].pipes[cmdBlock.num].fd[1], STDOUT_FILENO);
                    dup2(Clients[ID_serv].pipes[cmdBlock.num].fd[1], STDERR_FILENO);
                }else{
                    dup2(Clients[ID_serv].pipes[cmdBlock.num].fd[1], STDOUT_FILENO);
                    dup2(Clients[ID_serv].slave_sock, STDERR_FILENO);
                }
                close(Clients[ID_serv].pipes[cmdBlock.num].fd[1]);
            }else{
                dup2(Clients[ID_serv].slave_sock, STDOUT_FILENO);
				dup2(Clients[ID_serv].slave_sock, STDERR_FILENO);
            }
            // close all child user pipe
            for (int i=0; i<User_pipes.size(); i++){
					close(User_pipes[i].fd[0]);
					close(User_pipes[i].fd[1]);
			}
            //file redirection
            if(cmdBlock.pipeType == 2 && cmdBlock.HasSpace){
                int fd;
                what_is_next(Clients[ID_serv].next_cmdBlock.cmd, Clients[ID_serv].next_cmdBlock.argv);
                fd = open((Clients[ID_serv].next_cmdBlock.argv).back().data(), O_RDWR | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
                dup2(fd, STDOUT_FILENO);
                dup2(fd, STDERR_FILENO);
                close(fd);
            }
            //exec
            if(execvp(cmd.data(), argv) == -1)
                cerr  << "Unknown command: [" << cmd << "]." << endl;
            exit(EXIT_SUCCESS);
        default: //****parent process****
            //number pipes close
            if(cmdBlock.num == 0){
                for(int i=0; i < Clients[ID_serv].numpipes.size(); i++){            
                    if(Clients[ID_serv].numpipes[i].count == 0){
                        close(Clients[ID_serv].numpipes[i].fd[1]);
                        close(Clients[ID_serv].numpipes[i].fd[0]);
                        Clients[ID_serv].numpipes.erase(Clients[ID_serv].numpipes.begin()+i);
                        break;
                    }
                }
            }
            //close normal pipe
            if(cmdBlock.num != 0){
			    close(Clients[ID_serv].pipes[cmdBlock.num -1].fd[0]);
			    close(Clients[ID_serv].pipes[cmdBlock.num -1].fd[1]);
            }
            //close user pipe
            for (int i=0; i<User_pipes.size(); i++){
				if (User_pipes[i].fd[0] == Clients[ID_serv].pipes[cmdBlock.num].fd[0]){
					close(User_pipes[i].fd[0]);
					close(User_pipes[i].fd[1]);
					User_pipes.erase(User_pipes.begin()+i);
					break;
				}
			}
            //wait child process
			if (cmdBlock.pipeType == 3 || (cmdBlock.pipeType == 2 && cmdBlock.HasSpace) || cmdBlock.pipeType == 4){
				waitpid(child_pid, &status, 0);
			} else {
				waitpid(-1, &status, WNOHANG);
			}
        }
    }
    return true;
}

void string2arr(string cmd, vector<string> &argv, char *arr[]){
    int front = 0;
	int end;
    int index;
    int i;

    cmd += " ";

    //read arguments
    while((end = (cmd).find(" ", front)) != -1){
		if (end == front) {
			front = end + 1;
			continue;
		}
		(argv).push_back((cmd).substr(front, end-front));
		front = end + 1;
    }

    index = argv.size();
    if(index != 0){
        for(i=0; i<index; i++){
            arr[i] = (char*)argv[i].data();
        }
    }
    arr[argv.size()] = NULL;

}

void build_pipe(cmdBlock &cmdBlock){
    Pipe newPipe;
    bool merge = false;
    newPipe.count = -1;
    
    if(cmdBlock.num != 0){
        cmdBlock.read_pipe = true;
    }

    switch (cmdBlock.pipeType)
    {
    case 0: // "|"
        cmdBlock.write_pipe = true;
        newPipe.count = find_number();
        if(newPipe.count != -1)cmdBlock.skip = true;

        for(int i=0; i < Clients[ID_serv].numpipes.size(); i++){
            if(Clients[ID_serv].numpipes.size() != 0){
                if(Clients[ID_serv].numpipes[i].count == newPipe.count){
                    newPipe.fd[1] = Clients[ID_serv].numpipes[i].fd[1];
                    newPipe.fd[0] = Clients[ID_serv].numpipes[i].fd[0];
                    newPipe.count = -1;
                    merge = true;
                    break;
                }
            }
        }
        if(!merge){
            pipe(newPipe.fd);
        }
        break;
    
    case 1: // "!"
        cmdBlock.write_pipe = true;
        newPipe.count = find_number();
        if(newPipe.count != -1)cmdBlock.skip = true;
        for(int i=0; i < Clients[ID_serv].numpipes.size(); i++){
            if(Clients[ID_serv].numpipes.size() != 0){
                if(Clients[ID_serv].numpipes[i].count == newPipe.count){
                    newPipe.fd[1] = Clients[ID_serv].numpipes[i].fd[1];
                    newPipe.fd[0] = Clients[ID_serv].numpipes[i].fd[0];
                    newPipe.count = -1;
                    merge = true;
                    break;
                }
            }
        }
        if(!merge){
            pipe(newPipe.fd);
        }
        break;

    case 2: // ">"
        if(!cmdBlock.HasSpace){
            cmdBlock.write_pipe = true;
            int receiverID = find_number() -1;
            int senderID = ID_serv;
            bool type = 0; // 0: sending  1: receiving
            handle_UserPipe(newPipe, senderID, receiverID, type);
        }
        break;

    case 3: // "&"
        break;

    case 4: // "<"
        cmdBlock.read_pipe = true;
        int receiverID = ID_serv;
        int senderID = find_number() -1;
        bool type = 1; // 0: sending  1: receiving
        handle_UserPipe(newPipe, senderID, receiverID, type);
        break;
    }

    Clients[ID_serv].pipes.push_back(newPipe);
}

void what_is_next(string cmd, vector<string> &argv){

    int front = 0;
	int end;
    int index;
    int i;

    cmd += " ";

    //read arguments
    while((end = (cmd).find(" ", front)) != -1){
		if (end == front) {
			front = end + 1;
			continue;
		}
		(argv).push_back((cmd).substr(front, end-front));
		front = end + 1;
    }
    
}

int find_number(){
    int count = -1;

    what_is_next(Clients[ID_serv].next_cmdBlock.cmd, Clients[ID_serv].next_cmdBlock.argv);

    string last_argv = Clients[ID_serv].next_cmdBlock.argv.back();
    
    if (regex_match(last_argv, reg)){
        count = stoi(last_argv);
    }
    return count;
}
