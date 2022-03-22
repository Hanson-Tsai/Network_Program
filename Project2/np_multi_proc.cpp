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
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <cstring>
#include <algorithm>
#include <signal.h>

using namespace std;
//ssh hstsai@nplinux9.cs.nctu.edu.tw

#define MAXNUMPIPE 200
#define MAXUSER 30
#define MAXMSGNUM 10
#define MAXMSGSIZE 1024

#define SHMKEY ((key_t) 1012)
#define PERM 0666
const char base_dir[] = "/net/gcs/110/310552038";

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

typedef struct fifo{
    int	fd;
    char name[64 + 1];
}fifo_;

typedef struct client{
    bool has_user;
    char msg[MAXUSER][MAXMSGNUM][MAXMSGSIZE + 1];
    char address[16];
    int id;
    int port;
    int pid;
    char name[21];
    fifo_ fifo[MAXUSER];
}client;

// socketFunction---------------------------------------------------------------------
class socketFunction {
    public:
        static socketFunction& getInstance();
        int getMyID(void);
        client* getClientTable(void);
        //return UserName (handle username == "")
        string getClientName(string UserName);
        //remove all user info in clientTable
        void initiate_userdata(int index);
        //accept a User and set its data
        void Accepting_new_user(struct sockaddr_in addr);
        //show welcome message to the User with given ID
        void welcome_msg(int ID);
        //show login message of the User with given ID to all User
        void login_msg(int ID);
        //show logout message of the User with given ID to all User
        void logout_msg(int ID);
        //show who's on the server
        void who();
        //tell targetID the message
        void tell(int targetID, string Msg);
        //yell the message
        void yell(string Msg);
        //change the User's name
        void name(string newName);
        //broadcast message
        void broadcast(int *sourceID, int *targetID, string Msg);
        //check user pipe is avaliable
        bool check_UserPipe_Exist(int &index, int senderID, int receiverID);
        //check and build user pipe
        void handle_UserPipe(Pipe &newPipe, int senderID, int receiverID, bool type);
        //close fifo
        void remove_UserPipe(int senderID);
        
        //user id
        int ID_serv;
    private:
        //user table
        client* clientTable;
        socketFunction();
        ~socketFunction() = default;
};



//declare function calls -------------------------------------------------------

//allocate a share memory
void share_mem_init(void);

//destroy the share memory
void share_mem_destroy(int sig);

//handle signal
void sigal_handler(int sig);

//create master socket 
int create_socket(unsigned short port);

//serve ID:user
void Run_shell(struct sockaddr_in addr);

//cut each cmdline into cmdBlock
void cmd_packing(string cmdLine ,vector<cmdBlock> &cmdBlocks);

//execute the command
bool cmd_exe(cmdBlock &cmdBlock);

//change the string -> char *argv[]
void string2arr(string cmd, vector<string> &argv, char *arr[]);

//build the pipe for a cmdBlock
void build_pipe(cmdBlock &cmdBlock);

//find the next cmd in next cmdBlok and store it in next_cmdBlock
void what_is_next(string cmd, vector<string> &argv);

//find the number and return the number
int find_number();


// Regex ------------------------------------------------------------------------
static regex reg("[0-9]+");


// Global variable --------------------------------------------------------------
vector<Pipe> pipes;
vector<Pipe> numpipes;
vector<envVariable> envVariables;
cmdBlock next_cmdBlock;
string now_cmdLine;
int removeThisUserPipe = -1;


// Function ---------------------------------------------------------------------
void share_mem_init(void){
    int shmid = 0;
    client *clientTable;

    if((shmid = shmget(SHMKEY, MAXUSER * sizeof(client), PERM | IPC_CREAT)) < 0){
        cerr << "server err: shmget failed (errno #" << errno << ")" << endl;
        exit(1);
    }

    if((clientTable = (client*) shmat(shmid, NULL, 0)) == (client*) -1){
        cerr << "server err: shmat faild" << endl;
    }

    memset((char*) clientTable, 0, MAXUSER * sizeof(client));

    shmdt(clientTable);
}

void share_mem_destroy(int sig){
	if(sig == SIGCHLD){
		while (waitpid(-1, NULL, WNOHANG) > 0);
	}else if(sig == SIGINT || sig == SIGQUIT || sig == SIGTERM){
		int	shmid;
		if((shmid = shmget(SHMKEY, MAXUSER * sizeof (client), PERM)) < 0){
			cerr << "server err: shmget failed" << endl;
			exit (1);
		}
        client* clientTable = socketFunction::getInstance().getClientTable();
        for(int i = 0; i < MAXUSER; i++){
            for(int j = 0; i < MAXUSER; i++){
                if(clientTable[i].fifo[j].name[0] != 0){
                    close(clientTable[i].fifo[j].fd);
                    unlink(clientTable[i].fifo[j].name);
                }  
            }
        }
        
        if(shmctl(shmid, IPC_RMID, NULL) < 0){
            cerr << "server err: shmctl IPC_RMID failed" << endl;
			exit (1);
		}

		exit (0);
	}
	signal(sig, share_mem_destroy);
}

int main(int argc, char *argv[]){

    signal (SIGCHLD, share_mem_destroy);
	signal (SIGINT, share_mem_destroy);
	signal (SIGQUIT, share_mem_destroy);
	signal (SIGTERM, share_mem_destroy);

    struct sockaddr_in sin;
    unsigned int alen = sizeof(sin);
    int slave_sock, child_pid;

    if (argc != 2){
		return 0;
    }
    
    unsigned short port = (unsigned short)atoi(argv[1]);
    
    share_mem_init();

	int master_sock = create_socket(port);
	listen(master_sock, MAXUSER);
	while((slave_sock = accept(master_sock, (struct sockaddr *)&sin, &alen))){
        switch(child_pid = fork()){
            case 0:
                dup2(slave_sock, STDIN_FILENO);
                dup2(slave_sock, STDOUT_FILENO);
                dup2(slave_sock, STDERR_FILENO);
                close(master_sock);
                Run_shell(sin);
                close(slave_sock);
                exit(0);
            default:
                close(slave_sock);
        }
	}
    return 0;
}

void socketFunction::initiate_userdata(int index){
	for (int i = 0; i < MAXUSER; i++){
        if(clientTable[ID_serv].fifo[i].fd > 0){
            close(clientTable[ID_serv].fifo[i].fd);
            clientTable[ID_serv].fifo[i].fd = 0;
            unlink(clientTable[ID_serv].fifo[i].name);
            memset(clientTable[ID_serv].fifo[i].name, 0, 65);
        }
        if(clientTable[i].fifo[ID_serv].fd > 0){
            close(clientTable[i].fifo[ID_serv].fd);
            clientTable[i].fifo[ID_serv].fd = 0;
            unlink(clientTable[i].fifo[ID_serv].name);
            memset(clientTable[i].fifo[ID_serv].name, 0, 65);
        }            
    }
    //clear normal pipe
	for (int i=0; i<pipes.size(); i++){
		close(pipes[i].fd[0]);
		close(pipes[i].fd[1]);
	}
	pipes.clear();
    //clear num pipe
    for (int i=0; i<numpipes.size(); i++){
		close(numpipes[i].fd[0]);
		close(numpipes[i].fd[1]);
	}
    numpipes.clear();
    next_cmdBlock = {};
    memset(&clientTable[index], 0, sizeof(client));
    close (STDIN_FILENO);
    close (STDOUT_FILENO);
    close (STDERR_FILENO);
    shmdt(clientTable);
}

int create_socket(unsigned short port){
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
	return master_sock;
}

socketFunction& socketFunction::getInstance(){
    static socketFunction instance;
    return instance;
}

int socketFunction::getMyID(void){
    return this->ID_serv;
}

client* socketFunction::getClientTable(void){
    return this->clientTable;
}

string socketFunction::getClientName(string UserName){
	if (UserName == "") return "(no name)";
	else return UserName;
}

socketFunction::socketFunction(){
    int shmid = 0;

    if((shmid = shmget(SHMKEY, MAXUSER * sizeof(client), PERM)) < 0){
        cerr << "server err: shmget failed (errno #" << errno << ")" << endl;
        exit(1);
    }

    if((clientTable = (client*) shmat(shmid, NULL, 0)) == (client*) -1){
        cerr << "server err: shmat faild" << endl;
        exit(1);
    }
}

void socketFunction::Accepting_new_user(struct sockaddr_in sin_){
	for (int i = 0; i < MAXUSER; i++){
        if(!clientTable[i].has_user){
            clientTable[i].has_user = true;
            strncpy(clientTable[i].address, inet_ntoa(sin_.sin_addr), sizeof(clientTable[i].address) - 1);
            clientTable[i].address[sizeof(clientTable[i].address) - 1] = '\0';
            clientTable[i].pid = (int) getpid();
            clientTable[i].id = i;
            clientTable[i].port = htons(sin_.sin_port);
            ID_serv = i;
            welcome_msg(clientTable[i].id);
            login_msg(clientTable[i].id);
            break;
        }else{
            continue;
        }
    }
}

void socketFunction::welcome_msg(int ID){
	string Msg = "";
	Msg = Msg + "****************************************\n"
			  + "** Welcome to the information server. **\n"
			  + "****************************************\n";
	broadcast(NULL, &ID, Msg);
}

void socketFunction::login_msg(int ID){
	string msg = "*** User '" + getClientName(clientTable[ID].name) + "' entered from " + clientTable[ID].address + ":" + to_string(clientTable[ID].port) + ". ***\n";
	broadcast(NULL, NULL, msg);
}

void socketFunction::logout_msg(int ID){
	string Msg = "*** User '" + getClientName(clientTable[ID].name) + "' left. ***\n";
	broadcast(NULL, NULL, Msg);
}

void socketFunction::who(){
	string msg = "<ID>\t<nickname>\t<IP:port>\t<indicate me>\n";
	for (int i = 0; i < MAXUSER; i++){
		if (clientTable[i].has_user){
			msg += to_string(clientTable[i].id + 1) + "\t" + getClientName(clientTable[i].name) + "\t" + string(clientTable[i].address) + ":" + to_string(clientTable[i].port);
			if (clientTable[i].id == ID_serv){
				msg += "\t<-me";
			}
			msg += "\n";
		}
	}
	broadcast(NULL, &ID_serv, msg);
}

void socketFunction::tell(int targetID, string Msg){
	if (clientTable[targetID].has_user){
		Msg = "*** " + getClientName(clientTable[ID_serv].name) + " told you ***: " + Msg + "\n";
		broadcast(NULL, &targetID, Msg);
	} else {
		Msg = "*** Error: user #" + to_string(targetID + 1) + " does not exist yet. ***\n";
		broadcast(NULL, &ID_serv, Msg);
	}
}

void socketFunction::yell(string Msg){
	Msg = "*** " + getClientName(clientTable[ID_serv].name) + " yelled ***: " + Msg + "\n";
	broadcast(NULL, NULL, Msg);
}

void socketFunction::name(string newName){
	for (int i=0; i<MAXUSER; i++){
		if (i == ID_serv){
			continue;
		}
		if (clientTable[i].has_user && clientTable[i].name == newName){
			string Msg = "*** User '" + newName + "' already exists. ***\n";
			broadcast(NULL, &ID_serv, Msg);
			return;
		}
	}
	memset(clientTable[ID_serv].name, 0, sizeof(clientTable[ID_serv].name));
    strncpy(clientTable[ID_serv].name, newName.c_str(),newName.length());
	string Msg = "*** User from " + string(clientTable[ID_serv].address) + ":" + to_string(clientTable[ID_serv].port) + " is named '" + clientTable[ID_serv].name + "'. ***\n";
	broadcast(NULL, NULL, Msg);
}

void socketFunction::broadcast(int *sourceID, int *targetID, string origin_Msg){
	if (targetID == NULL){
		for (int i = 0; i < MAXUSER; i++){
			if (clientTable[i].has_user){
                for(int j = 0; j < MAXMSGNUM; j++){
                    if(clientTable[i].msg[ID_serv][j][0] == 0){
                        strncpy(clientTable[i].msg[ID_serv][j], origin_Msg.c_str(), MAXMSGSIZE + 1);
                        kill(clientTable[i].pid, SIGUSR1);
                        break;
                    }
                }
			}
		}
	} else {
        if (*targetID == ID_serv){
            write(STDOUT_FILENO, origin_Msg.c_str(), sizeof(char) * origin_Msg.length());
        }else{
            for (int i = 0; i < MAXMSGNUM; i++){
                if(clientTable[*targetID].msg[*targetID][i][0] == 0){
                    strncpy(clientTable[*targetID].msg[ID_serv][i], origin_Msg.c_str(), MAXMSGSIZE + 1);
                    kill(clientTable[*targetID].pid, SIGUSR1);
                    break;
                }
            }
        }
	}
}

bool socketFunction::check_UserPipe_Exist(int &index, int senderID, int receiverID){
	bool IsExist = true;
	if(clientTable[senderID].fifo[receiverID].name[0] == 0){
        IsExist = false;
    }
	return IsExist;
}

void socketFunction::handle_UserPipe(Pipe &newPipe, int senderID, int receiverID, bool type){
    int index;
    int FD_NULL = open("/dev/null", O_RDWR);
    //sending 
    if(type == 0){
        if (receiverID < 0 || receiverID  > 29 || !clientTable[receiverID].has_user){
			string Msg = "*** Error: user #" + to_string(receiverID + 1) + " does not exist yet. ***\n";
			broadcast(NULL, &senderID, Msg);
            newPipe.fd[1] = FD_NULL;
            newPipe.fd[0] = -2;
		} else {
			if (check_UserPipe_Exist(index, senderID, receiverID)){
				string Msg = "*** Error: the pipe #" + to_string(senderID + 1) + "->#" + to_string(receiverID + 1) + " already exists. ***\n";
				broadcast(NULL, &senderID, Msg);
                newPipe.fd[1] = FD_NULL;
		        newPipe.fd[0] = -2;
			} else {
				string Msg = "*** " + getClientName(clientTable[senderID].name) + " (#" + to_string(senderID + 1) + ") just piped '" + now_cmdLine + "' to "
							+ getClientName(clientTable[receiverID].name) + " (#" + to_string(receiverID + 1) + ") ***\n";
				broadcast(NULL, NULL, Msg);
                string fileName = "/fifo" + to_string(senderID) + "->" + to_string(receiverID);
                strncpy(clientTable[senderID].fifo[receiverID].name, base_dir, 65);
                strcat(clientTable[senderID].fifo[receiverID].name, fileName.c_str());
                if(mkfifo(clientTable[senderID].fifo[receiverID].name, 0600) < 0){
                    cerr << "error: failed to create FIFO (" + string(clientTable[senderID].fifo[receiverID].name) + ") #" << errno << endl;
                }else{
                    kill(clientTable[receiverID].pid, SIGUSR2);
                    newPipe.fd[1] = open(clientTable[senderID].fifo[receiverID].name, O_WRONLY);
                    newPipe.fd[0] = -2;
                }
			}
		}
    }
    //receiving
    if(type == 1){
        if (senderID < 0 || senderID > 29 || !clientTable[senderID].has_user){
			string Msg = "*** Error: user #" + to_string(senderID + 1) + " does not exist yet. ***\n";
			broadcast(NULL, &receiverID, Msg);
            newPipe.fd[0] = FD_NULL;
	        newPipe.fd[1] = -2;
		} else {
			if (check_UserPipe_Exist(index, senderID, receiverID)){
				string Msg = "*** " + getClientName(clientTable[receiverID].name) + " (#" + to_string(receiverID + 1) + ") just received from "
							+ getClientName(clientTable[senderID].name) + " (#" + to_string(senderID + 1) + ") by '" + now_cmdLine + "' ***\n";
				broadcast(NULL, NULL, Msg);
                client* clientTable = socketFunction::getInstance().getClientTable();
				newPipe.fd[0] = clientTable[senderID].fifo[receiverID].fd;
				newPipe.fd[1] = -2;
                removeThisUserPipe = senderID;
			} else {
				string Msg = "*** Error: the pipe #" + to_string(senderID + 1) + "->#" + to_string(receiverID + 1) + " does not exist yet. ***\n";
				broadcast(NULL, &receiverID, Msg);
                newPipe.fd[0] = FD_NULL;
		        newPipe.fd[1] = -2;
			}
		}
    }
}

void socketFunction::remove_UserPipe(int senderID){
    int receiverID = ID_serv;
    close(clientTable[senderID].fifo[receiverID].fd);
    clientTable[senderID].fifo[receiverID].fd = 0;
    unlink(clientTable[senderID].fifo[receiverID].name);
    memset(clientTable[senderID].fifo[receiverID].name, 0, 65);
}

void sigal_handler(int sig){
    client *clientTable = socketFunction::getInstance().getClientTable();
    int uid = socketFunction::getInstance().getMyID();
    if(sig == SIGUSR1){ // receive massage
		int	i, j;
		for (i = 0; i < MAXUSER; ++i) {
			for (j = 0; j < MAXMSGNUM; ++j) {
				if (clientTable[uid].msg[i][j][0] != 0) {
                    write(STDOUT_FILENO, clientTable[uid].msg[i][j], strlen (clientTable[uid].msg[i][j]));
					memset (clientTable[uid].msg[i][j], 0, MAXMSGSIZE);
				}
			}
		}
	}else if(sig == SIGUSR2){ // handle user pipe
        for(int i = 0; i < MAXUSER; i++){
            if(clientTable[i].fifo[uid].fd == 0 && clientTable[i].fifo[uid].name[0] != 0){
                clientTable[i].fifo[uid].fd = open(clientTable[i].fifo[uid].name, O_RDONLY | O_NONBLOCK);
            }
        }
        
    }

	signal(sig, sigal_handler);
}



//-------------------------------------------------------------------------------
void Run_shell(struct sockaddr_in addr){
    signal(SIGUSR1, sigal_handler);
    signal(SIGUSR2, sigal_handler);
    socketFunction::getInstance().Accepting_new_user(addr);

    int ID_serv = socketFunction::getInstance().getMyID();
    // envirovment setup
    envVariable new_envVariable;
    new_envVariable.name = "PATH";
    new_envVariable.value = "bin:.";
    envVariables.push_back(new_envVariable);
	clearenv();
	for (int i=0; i<envVariables.size(); i++){
		setenv(envVariables[i].name.data(), envVariables[i].value.data(), 1);
	}

    string cmdLine;
    vector<cmdBlock> cmdBlocks;
    bool doneServe = true;

    while(true){

        if (doneServe){
            cout << "% ";
            doneServe = false;
        }
        getline(cin, cmdLine);
        if (cmdLine[cmdLine.length()-1] == '\r'){
			cmdLine = cmdLine.substr(0, cmdLine.length() - 1);
		}
        if (cmdLine.length() == 0){
            doneServe = true;
            continue;
        }

        now_cmdLine = cmdLine;
        cmd_packing(cmdLine, cmdBlocks);

        while(!cmdBlocks.empty()){
            if(cmdBlocks[0].pipeType != 3){
                next_cmdBlock = cmdBlocks[1]; //prefatch cmd to be global variable
            }
                
            if (!cmd_exe(cmdBlocks[0])){
                return;
            }
                
            if(cmdBlocks[0].skip == true){
                cmdBlocks.erase(cmdBlocks.begin());
            }
                
            cmdBlocks.erase(cmdBlocks.begin());
            next_cmdBlock = {};
        }

        // reduction of num pipe
        int n =  pipes.size(); 
        for(int i=0; i < n; i++){
            if(pipes.size() != 0){
                if(pipes[0].count > 0){
                    numpipes.push_back(pipes[0]);
                }
                pipes.erase(pipes.begin());
            }
        }

        for(int i=0; i <= numpipes.size(); i++){
            if(numpipes.size() != 0){
                numpipes[i].count--;
            }
        }
        
        doneServe = true;
    }
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
    int ID_serv = socketFunction::getInstance().getMyID();
    int child_pid ,status;
    char *argv[1000];
    string cmd;

    string2arr(cmdBlock.cmd, cmdBlock.argv, argv);
    cmd = cmdBlock.argv[0].data();
    
    if (cmd == "printenv"){
        if(getenv(cmdBlock.argv[1].data()) != NULL){
            string Msg = getenv(cmdBlock.argv[1].data());
			socketFunction::getInstance().broadcast(NULL, &ID_serv, Msg + "\n");
        }
    } else if(cmd == "setenv"){
		envVariable new_envVariable;
		new_envVariable.name = cmdBlock.argv[1];
		new_envVariable.value = cmdBlock.argv[2];
		envVariables.push_back(new_envVariable);
		setenv(cmdBlock.argv[1].data(), cmdBlock.argv[2].data(), 1);
    } else if (cmd == "who"){
		socketFunction::getInstance().who();
	} else if (cmd == "tell"){
		string Msg = cmdBlock.cmd.substr(cmdBlock.cmd.find(cmdBlock.argv[2]));
		socketFunction::getInstance().tell(stoi(cmdBlock.argv[1])-1, Msg);
	} else if (cmd == "yell"){
		string Msg = cmdBlock.cmd.substr(cmdBlock.cmd.find(cmdBlock.argv[1]));
		socketFunction::getInstance().yell(Msg);
	} else if (cmd == "name"){
		socketFunction::getInstance().name(cmdBlock.argv[1]);
    } else if(cmd == "exit"){
        socketFunction::getInstance().logout_msg(ID_serv);
		socketFunction::getInstance().initiate_userdata(ID_serv);
		pid_t wpid;
        int status = 0;
        while ((wpid = wait(&status)) > 0) {}
		exit(0);
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
                for(int i=0; i <= numpipes.size(); i++){            
                    if(numpipes.size() == 0){ i++;}
                    else if(numpipes[i].count == 0){
                        close(numpipes[i].fd[1]);
                        dup2(numpipes[i].fd[0], STDIN_FILENO);
                        close(numpipes[i].fd[0]);
                    }
                }
            }
            // connect other pipe
            if(cmdBlock.read_pipe){
                if(cmdBlock.pipeType == 4){
                    close(pipes[cmdBlock.num].fd[1]);
                    dup2(pipes[cmdBlock.num].fd[0], STDIN_FILENO);
                    close(pipes[cmdBlock.num].fd[0]);
                }else{
                close(pipes[cmdBlock.num - 1].fd[1]);
                dup2(pipes[cmdBlock.num - 1].fd[0], STDIN_FILENO);
                close(pipes[cmdBlock.num - 1].fd[0]);
                }
            }
            if(cmdBlock.write_pipe){
                close(pipes[cmdBlock.num].fd[0]);
                if(cmdBlock.pipeType == 1){
                    dup2(pipes[cmdBlock.num].fd[1], STDOUT_FILENO);
                    dup2(pipes[cmdBlock.num].fd[1], STDERR_FILENO);
                }else{
                    dup2(pipes[cmdBlock.num].fd[1], STDOUT_FILENO);
                }
                close(pipes[cmdBlock.num].fd[1]);
            }
            //file redirection
            if(cmdBlock.pipeType == 2 && cmdBlock.HasSpace){
                int fd;
                what_is_next(next_cmdBlock.cmd, next_cmdBlock.argv);
                fd = open((next_cmdBlock.argv).back().data(), O_RDWR | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
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
                for(int i=0; i < numpipes.size(); i++){            
                    if(numpipes[i].count == 0){
                        close(numpipes[i].fd[1]);
                        close(numpipes[i].fd[0]);
                        numpipes.erase(numpipes.begin()+i);
                        break;
                    }
                }
            }
            //close normal pipe
            if(cmdBlock.num != 0){
			    close(pipes[cmdBlock.num -1].fd[0]);
			    close(pipes[cmdBlock.num -1].fd[1]);
            }
            //close user pipe
            if(removeThisUserPipe >= 0){
                socketFunction::getInstance().remove_UserPipe(removeThisUserPipe);
                removeThisUserPipe = -1;
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
    int ID_serv = socketFunction::getInstance().getMyID();
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

        for(int i=0; i < numpipes.size(); i++){
            if(numpipes.size() != 0){
                if(numpipes[i].count == newPipe.count){
                    newPipe.fd[1] = numpipes[i].fd[1];
                    newPipe.fd[0] = numpipes[i].fd[0];
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
        for(int i=0; i < numpipes.size(); i++){
            if(numpipes.size() != 0){
                if(numpipes[i].count == newPipe.count){
                    newPipe.fd[1] = numpipes[i].fd[1];
                    newPipe.fd[0] = numpipes[i].fd[0];
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
            socketFunction::getInstance().handle_UserPipe(newPipe, senderID, receiverID, type);
        }
        break;

    case 3: // "&"
        break;

    case 4: // "<"
        cmdBlock.read_pipe = true;
        int receiverID = ID_serv;
        int senderID = find_number() -1;
        bool type = 1; // 0: sending  1: receiving
        socketFunction::getInstance().handle_UserPipe(newPipe, senderID, receiverID, type);
        break;
    }

    pipes.push_back(newPipe);
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

    what_is_next(next_cmdBlock.cmd, next_cmdBlock.argv);

    string last_argv = next_cmdBlock.argv.back();
    
    if (regex_match(last_argv, reg)){
        count = stoi(last_argv);
    }
    return count;
}