#include <iostream>
#include <string.h>
#include <vector>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <regex>
#include <stdlib.h>
using namespace std;
//ssh hstsai@nplinux9.cs.nctu.edu.tw

//Struct ------------------------------------------------------------------------
typedef struct cmdBlock{
	string cmd;
	vector<string> argv;
	bool read_pipe, write_pipe;
	int pipeType; //0: "|",   1: "!",   2: ">",   3: "no pipe"
    int num;
    bool skip;
}cmdBlock;

typedef struct Pipe{
	int count;
	int fd[2];
}Pipe;


//declare function calls
void init_shell();

int cmd_packing(string cmdLine ,vector<cmdBlock> &cmdBlocks);

void cmd_exe(cmdBlock &cmdBlock);

void string2arr(string cmd, vector<string> &argv, char *arr[]);

void build_pipe(cmdBlock &cmdBlock);

void what_is_next(string cmd, vector<string> &argv);

int find_num_pipe();

// Global variable
static regex reg("[0-9]+");
vector<Pipe> pipes;
vector<Pipe> numpipes;
cmdBlock pre_cmdBlock;


//Main Function -----------------------------------------------------------------
int main(){
    int run_shell = 1;
    int num_of_cmd;
    string cmdLine;
    vector<cmdBlock> cmdBlocks;

    init_shell();
    
    while(run_shell){
        
        num_of_cmd =0;
        cout << "% ";
        getline(cin,cmdLine);
        
        num_of_cmd = cmd_packing(cmdLine, cmdBlocks);
        
        while(!cmdBlocks.empty()){

            if(cmdBlocks[0].pipeType != 3){
                pre_cmdBlock = cmdBlocks[1]; //prefatch cmd to be global variable
            }
            
            cmd_exe(cmdBlocks[0]);
            
            if(cmdBlocks[0].skip == true){
                cmdBlocks.erase(cmdBlocks.begin());
            }
            
            cmdBlocks.erase(cmdBlocks.begin());

        }
        
        
        for(int i=0; i < num_of_cmd; i++){
            
            if(pipes.size() != 0){
                if(pipes[0].count > 0){
                    //cout << pipes[0].count << endl;
                    numpipes.push_back(pipes[0]);
                }
                pipes.erase(pipes.begin());
            }
        }

        for(int i=0; i<= numpipes.size(); i++){
            if(numpipes.size() != 0){
                numpipes[i].count--;
            }
        }

    }
    return 0;
}




// Function call ---------------------------------------------------------------
void init_shell(){
	setenv("PATH", "bin:.", 1);
}


int cmd_packing(string cmdLine ,vector<cmdBlock> &cmdBlocks){
    int last = 0;
    int start = 0;
    int index = 0;
    int num_of_cmd = 0;
    cmdBlock cmdBlock;
    string tmp;
    
    cmdLine += "&";

    do{
        //initial cmdBlock
        cmdBlock.read_pipe = false;
		cmdBlock.write_pipe = false;
		cmdBlock.pipeType = 0;
        cmdBlock.skip = false;

        //spilt cmd
        if(cmdLine[index] == '|'){
            num_of_cmd++;
            cmdBlock.num = num_of_cmd - 1;
            cmdBlock.cmd.assign(cmdLine, start, index-start);
            cmdBlock.pipeType = 0;
            cmdBlocks.push_back(cmdBlock);
            index++;
            start = index;
            
        }else if(cmdLine[index] == '!'){
            num_of_cmd++;
            cmdBlock.num = num_of_cmd - 1;
            cmdBlock.cmd.assign(cmdLine, start, index-start);
            cmdBlock.pipeType = 1;
            cmdBlocks.push_back(cmdBlock);
            index++;
            start = index;
        }else if(cmdLine[index] == '>'){
            num_of_cmd++;
            cmdBlock.num = num_of_cmd - 1;
            cmdBlock.cmd.assign(cmdLine, start, index-start);
            cmdBlock.pipeType = 2;
            cmdBlock.skip = true;
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
        }else{
            index++;
        }
        
    }while(!last);

    return num_of_cmd;
}


void cmd_exe(cmdBlock &cmdBlock){
    int child_pid;
    char *argv[1000];
    string cmd;

    //change cmd from string to array
    string2arr(cmdBlock.cmd, cmdBlock.argv, argv);
    cmd = cmdBlock.argv[0].data();
    
    
    
    if(cmd == "printenv"){
        if(getenv(cmdBlock.argv[1].data()) != NULL)
            cout << getenv(cmdBlock.argv[1].data()) << endl;
    }else if(cmd == "setenv"){
		setenv(cmdBlock.argv[1].data(), cmdBlock.argv[2].data(), 1);
    }else if(cmd == "exit"){
        exit(0);
    }else if(cmd == "nothing_enter"){
        //do nothing
    }
    else{
    
        int status;

        build_pipe(cmdBlock); //build pipe

        while((child_pid = fork()) < 0){
			while(waitpid(-1, &status, WNOHANG) > 0); //fork error
            exit(EXIT_FAILURE);
		}

        switch (child_pid)
        {
        case 0: //child process
            if(cmdBlock.num == 0){
                for(int i=0; i <= numpipes.size(); i++){            //number pipes to stdin
                    if(numpipes.size() == 0){ i++;}
                    else if(numpipes[i].count == 0){
                        close(numpipes[i].fd[1]);
                        dup2(numpipes[i].fd[0], STDIN_FILENO);
                        close(numpipes[i].fd[0]);
                    }
                }
            }
            if(cmdBlock.read_pipe){
                close(pipes[cmdBlock.num - 1].fd[1]);
                dup2(pipes[cmdBlock.num - 1].fd[0], STDIN_FILENO);
                close(pipes[cmdBlock.num - 1].fd[0]);
            }
            if(cmdBlock.write_pipe){
                close(pipes[cmdBlock.num].fd[0]);
                dup2(pipes[cmdBlock.num].fd[1], STDOUT_FILENO);
                if(cmdBlock.pipeType == 1){
                    dup2(pipes[cmdBlock.num].fd[1], STDERR_FILENO);
                }
                close(pipes[cmdBlock.num].fd[1]);
            }
            //file redirection
            if(cmdBlock.pipeType == 2){
                int fd;
                what_is_next(pre_cmdBlock.cmd, pre_cmdBlock.argv);
                fd = open((pre_cmdBlock.argv).back().data(), O_RDWR | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
                dup2(fd, STDOUT_FILENO);
                dup2(fd, STDERR_FILENO);
                close(fd);
            }
            if(execvp(cmd.data(), argv) == -1)
                cerr  << "Unknown command: [" << cmd << "]." << endl;
            exit(EXIT_SUCCESS);
        default: //parent process
            if(cmdBlock.num == 0){
                for(int i=0; i <= numpipes.size(); i++){            //number pipes close
                    if(numpipes.size() == 0){ i++;}
                    else if(numpipes[i].count == 0){
                        close(numpipes[i].fd[1]);
                        close(numpipes[i].fd[0]);
                        numpipes.erase(numpipes.begin()+i);
                    }
                }
            }
            if(cmdBlock.num != 0){
			    close(pipes[cmdBlock.num -1].fd[0]);
			    close(pipes[cmdBlock.num -1].fd[1]);
            }		
			if (cmdBlock.pipeType == 3 || cmdBlock.pipeType == 2){
				waitpid(child_pid, &status, 0);
			} else {
				waitpid(-1, &status, WNOHANG);
			}
        }
    }

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
        newPipe.count = find_num_pipe();
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
        pipe(newPipe.fd);
        cmdBlock.write_pipe = true;
        newPipe.count = find_num_pipe();
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
        break;

    case 3: // "&"
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

int find_num_pipe(){
    int count = -1;

    what_is_next(pre_cmdBlock.cmd, pre_cmdBlock.argv);

    string last_argv = pre_cmdBlock.argv.back();
    
    if (regex_match(last_argv, reg)){
        count = stoi(last_argv);
    }

    return count;
}