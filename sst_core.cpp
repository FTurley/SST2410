#include <sst/core/sst_config.h>
#include <sst_core/sst_core.hpp>
#include <sstream>
#include <sst/core/interfaces/simpleMem.h>
#include <sst/core/interfaces/stringEvent.h>
#include <sst/core/simulation.h>
#include <cstdlib>
#include <math.h>
#include <fstream>

#define print_request(req_to_print) \
{\
std::cout<<\
(req_to_print->cmd==Interfaces::SimpleMem::Request::Command::Write?"Write":\
(req_to_print->cmd==Interfaces::SimpleMem::Request::Command::Read?"Read":\
(req_to_print->cmd==Interfaces::SimpleMem::Request::Command::ReadResp?"ReadResp":\
(req_to_print->cmd==Interfaces::SimpleMem::Request::Command::WriteResp?"WriteResp":"Some flush"\
))))<<std::endl<<\
"\tTo Address "<<req_to_print->addr<<std::endl<<\
"\tSize "<<req_to_print->size<<std::endl<<\
"\tId "<<req_to_print->id<<std::endl;\
}

namespace XSim
{
namespace SST
{
int in_mul = 0;
int in_div = 0;
int in_ls = 0;
int in_int = 0;


struct node{
int remaining_cycles;
int memloc;
int iready;
int jready;
int executing;
int complete;
int destreg;
int read;
node *ires;
node *jres;
node *next;
node *prev;

};
typedef struct node *nodep;

nodep beginning_of_int;
nodep end_of_int;
nodep reg_file[8];
int int_empty = 1;
int using_int = 0;

nodep beginning_of_div;
nodep end_of_div;
int div_empty = 1;
int using_div = 0;

nodep beginning_of_mul;
nodep end_of_mul;
int mul_empty = 1;
int using_mul = 0;

nodep beginning_of_ls;
int ls_empty = 1;
int using_ls = 0;
int to_dec_ls = 0;


core::core(ComponentId_t id, Params& params):
Component(id)
{
//***********************************************************************************************
//*	Some of the parameters may not be avaiable here, e.g. if --run-mode is "init"				*
//*		As a consequence, this section shall not throw any errors. Instead use setup section	*
//***********************************************************************************************

// verbose is one of the configuration options for this component
verbose = (uint32_t) params.find<uint32_t>("verbose", verbose);
// clock_frequency is one of the configuration options for this component
clock_frequency=params.find<std::string>("clock_frequency",clock_frequency);


n_read_requests=params.find<uarch_t>("n_read_requests",n_read_requests);
n_write_requests=params.find<uarch_t>("n_write_requests",n_write_requests);

int_num=params.find<uint16_t>("integernum",int_num);
int_res=params.find<uint16_t>("integerres",int_res);
int_lat=params.find<uint16_t>("integerlat",int_lat);
div_num=params.find<uint16_t>("dividernum",div_num);
div_res=params.find<uint16_t>("dividerres",div_res);
div_lat=params.find<uint16_t>("dividerlat",div_lat);
mul_num=params.find<uint16_t>("multipliernum",mul_num);
mul_res=params.find<uint16_t>("multiplierres",mul_res);
mul_lat=params.find<uint16_t>("multiplierlat",mul_lat);
ls_res=params.find<uint16_t>("lsres",ls_res);
ls_num=params.find<uint16_t>("lsnum",ls_num);
ls_lat=params.find<uint16_t>("lslat",ls_lat);

program_file=params.find<std::string>("program",program_file);
output_file=params.find<std::string>("output",output_file);



// Create the SST output with the required verbosity level
output = new Output("mips_core[@t:@l]: ", verbose, 0, Output::STDOUT);

bool success;
output->verbose(CALL_INFO, 1, 0, "Configuring cache connection...\n");

//
// The following code creates the SST interface that will connect to the memory hierarchy
//
data_memory_link = dynamic_cast<SimpleMem*>(Super::loadModuleWithComponent("memHierarchy.memInterface", this, params));
SimpleMem::Handler<core> *data_handler=
		new SimpleMem::Handler<core>(this,&core::memory_callback);
if(!data_memory_link)
{
	output->fatal(CALL_INFO, -1, "Error loading memory interface module.\n");
}
else
{
	output->verbose(CALL_INFO, 1, 0, "Loaded memory interface successfully.\n");
}
success=data_memory_link->initialize("data_memory_link", data_handler );
if(success)
{
	output->verbose(CALL_INFO, 1, 0, "Loaded memory initialize routine returned successfully.\n");
}
else
{
	output->fatal(CALL_INFO, -1, "Failed to initialize interface: %s\n", "memHierarchy.memInterface");
}
output->verbose(CALL_INFO, 1, 0, "Configuration of memory interface completed.\n");

// tell the simulator not to end without us
registerAsPrimaryComponent();
primaryComponentDoNotEndSim();
}


void core::setup()
{
output->output("Setting up.\n");
Super::registerExit();

// Create a tick function with the frequency specified
Super::registerClock( clock_frequency, new Clock::Handler<core>(this, &core::tick ) );

// Memory latency is used to make write/read requests to the SST simulated memory
//  Simple wrapper to register callbacks
memory_latency = new SSTMemory(data_memory_link);
core::loadProgram();
for(int i=0; i<8; i++){
	registers[i]=0;
for(int i=0; i<22; i++){
	counts[i]=0;
}
cycles=0;
instructions=0;
}
}


void core::init(unsigned int phase)
{
output->output("Initializing.\n");
//Nothing to do here
}

void core::loadProgram(){
printf("%d\n",ls_res );
printf("%d\n",ls_lat );

programArray = (uint16_t*)malloc(65536*sizeof(uint16_t));
data_memory = (int16_t*)malloc(65536*sizeof(int16_t));
for(int i=0;i<8;i++)
	reg_file[i]=NULL;


std::ifstream file(program_file);
    std::string str;
    while (std::getline(file, str))
    {
			if(str.front() != '#'){
				if(str.length() > 5){
					programArray[instr_count] = (int)strtol(str.substr(0,4).c_str(), NULL, 16);
					//printf("%d\n",programArray[instr_count]);
					data_memory[instr_count++] = (int)strtol(str.substr(5,8).c_str(), NULL, 16);
					//printf("%d\n",data_memory[instr_count-1]);
				}
				else
					programArray[instr_count++] = (int)strtol(str.c_str(), NULL, 16);
    }
	}
}

void core::finish()
{
/*
FILE * pFile;
pFile = fopen (output_file.c_str(),"w");
fprintf(pFile,"{\"registers\":[\n\t{\"r0\":%d,\"r1\":%d,\"r2\":%d,\"r3\":%d,\n\t \"r4\":%d,\"r5\":%d,\"r6\":%d,\"r7\":%d\n\t}],",registers[0],registers[1],registers[2],registers[3],registers[4],registers[5],registers[6],registers[7]);
fprintf(pFile,"\n\"stats\":[\n\t{\"add\":%d,\"sub\":%d,\"and\":%d,\n",counts[0],counts[1],counts[2]);
fprintf(pFile,"\t \"nor\":%d,\"div\":%d,\"mul\":%d,\n",counts[3],counts[4],counts[5]);
fprintf(pFile,"\t \"mod\":%d,\"exp\":%d,\"lw\":%d,\n",counts[6],counts[7],counts[8]);
fprintf(pFile,"\t \"sw\":%d,\"liz\":%d,\"lis\":%d,\n",counts[9],counts[10],counts[11]);
fprintf(pFile,"\t \"lui\":%d,\"bp\":%d,\"bn\":%d,\n",counts[12],counts[13],counts[14]);
fprintf(pFile,"\t \"bx\":%d,\"bz\":%d,\"jr\":%d,\n",counts[15],counts[16],counts[17]);
fprintf(pFile,"\t \"jalr\":%d,\"j\":%d,\"halt\":%d,\n",counts[18],counts[19],counts[20]);
fprintf(pFile,"\t \"put\":%d,\n",counts[21]);
fprintf(pFile,"\t \"instructions\":%d,\n",instructions);
fprintf(pFile,"\t \"cycles\":%d\n",cycles);
fprintf(pFile,"\t}]\n}");
fclose(pFile);
*/
}

bool core::tick(Cycle_t cycle)
{
printf("Ticked\n");

//***********************************************************************************************
//*	What you need to do is to change the logic in this function with instruction execution *
//***********************************************************************************************

//std::cout<<"core::tick"<<std::endl;
cycles++;


std::function<void(uarch_t, uarch_t)> callback_function = [this](uarch_t request_id, uarch_t addr)
{
	nodep current = beginning_of_ls;

	//Broadcast first to allow those waiting to immediately read
			current->complete = 1;
			to_dec_ls++;
			if(current->destreg > -1 && reg_file[current->destreg] == current)
				reg_file[current->destreg] = NULL;

			if(current->next != NULL)
				beginning_of_ls = current->next;

			else{
				beginning_of_ls = NULL;
				ls_empty = 1;
			}
			printf("Ls broadcasted\n");
};





nodep current = beginning_of_int;
int to_dec_int = 0;

//Broadcast first to allow those waiting to immediately read
while(current != NULL){
	if(current->remaining_cycles == 0 && current->executing == 1){
		current->complete = 1;
		to_dec_int++;
		if(current->destreg > -1 && reg_file[current->destreg] == current)
			reg_file[current->destreg] = NULL;

		if(current->prev != NULL && current->next != NULL){
			current->prev->next = current->next;
			current->next->prev = current->prev;
		}
		else if(current->prev == NULL && current-> next != NULL){
			beginning_of_int = current->next;
			beginning_of_int->prev = NULL;
		}
		else if(current->prev != NULL && current-> next == NULL){
			current->prev->next = NULL;
			end_of_int = current->prev;
		}
		else if(current->prev == NULL && current-> next == NULL){
			beginning_of_int = NULL;
			end_of_int = NULL;
			int_empty = 1;
		}
		printf("Something broadcasted\n");
	}
	if(current->next == NULL)
		break;
	else
		current = current->next;
}

current = beginning_of_div;
int to_dec_div = 0;

//Broadcast first to allow those waiting to immediately read
while(current != NULL){
	if(current->remaining_cycles == 0 && current->executing == 1){
		current->complete = 1;
		to_dec_div++;
		if(current->destreg > -1 && reg_file[current->destreg] == current)
			reg_file[current->destreg] = NULL;

		if(current->prev != NULL && current->next != NULL){
			current->prev->next = current->next;
			current->next->prev = current->prev;
		}
		else if(current->prev == NULL && current-> next != NULL){
			beginning_of_div = current->next;
			beginning_of_div->prev = NULL;
		}
		else if(current->prev != NULL && current-> next == NULL){
			current->prev->next = NULL;
			end_of_div = current->prev;
		}
		else if(current->prev == NULL && current-> next == NULL){
			beginning_of_div = NULL;
			end_of_div = NULL;
			div_empty = 1;
		}
		printf("Something broadcasted\n");
	}
	if(current->next == NULL)
		break;
	else
		current = current->next;
}

current = beginning_of_mul;
int to_dec_mul = 0;

//Broadcast first to allow those waiting to immediately read
while(current != NULL){
	if(current->remaining_cycles == 0 && current->executing == 1){
		current->complete = 1;
		to_dec_mul++;
		if(current->destreg > -1 && reg_file[current->destreg] == current)
			reg_file[current->destreg] = NULL;

		if(current->prev != NULL && current->next != NULL){
			current->prev->next = current->next;
			current->next->prev = current->prev;
		}
		else if(current->prev == NULL && current-> next != NULL){
			beginning_of_mul = current->next;
			beginning_of_mul->prev = NULL;
		}
		else if(current->prev != NULL && current-> next == NULL){
			current->prev->next = NULL;
			end_of_mul = current->prev;
		}
		else if(current->prev == NULL && current-> next == NULL){
			beginning_of_mul = NULL;
			end_of_mul = NULL;
			mul_empty = 1;
		}
		printf("Something broadcasted\n");
	}
	if(current->next == NULL)
		break;
	else
		current = current->next;
}

current = beginning_of_int;

while(current != NULL){
	if(current->iready == 0){
		if(current->ires->complete == 1){
			current->iready = 1;
			current->ires = NULL;
		}
	}
	if(current->jready == 0){
		if(current->jres->complete == 1){
			current->jready = 1;
			current->jres = NULL;
		}
	}

	if(current->iready == 1 && current->jready == 1 && current->executing == 0 ){
		if(current->read == 1 && using_int < int_num){
			current->executing = 1;
			printf("Something executed one cycle\n");
			current->remaining_cycles-=1;
			using_int++;
		}
		else if(current->read == 0){
			current->read = 1;
			printf("Read instructions\n");
		}
	}

	else if(current->executing == 1){
		current->remaining_cycles-=1;
		printf("Something executed one cycle\n");
	}

	if(current->next == NULL)
		break;
	else
		current = current->next;
}


current = beginning_of_div;

while(current != NULL){
	if(current->iready == 0){
		if(current->ires->complete == 1){
			current->iready = 1;
			current->ires = NULL;
		}
	}
	if(current->jready == 0){
		if(current->jres->complete == 1){
			current->jready = 1;
			current->jres = NULL;
		}
	}

	if(current->iready == 1 && current->jready == 1 && current->executing == 0 ){
		if(current->read == 1 && using_div < div_num){
			current->executing = 1;
			printf("Something executed one cycle\n");
			current->remaining_cycles-=1;
			using_div++;
		}
		else if(current->read == 0){
			current->read = 1;
			printf("Read instructions\n");
		}
	}

	else if(current->executing == 1){
		current->remaining_cycles-=1;
		printf("Something executed one cycle\n");
	}

	if(current->next == NULL)
		break;
	else
		current = current->next;
}

current = beginning_of_mul;

while(current != NULL){
	if(current->iready == 0){
		if(current->ires->complete == 1){
			current->iready = 1;
			current->ires = NULL;
		}
	}
	if(current->jready == 0){
		if(current->jres->complete == 1){
			current->jready = 1;
			current->jres = NULL;
		}
	}

	if(current->iready == 1 && current->jready == 1 && current->executing == 0 ){
		if(current->read == 1 && using_mul < mul_num){
			current->executing = 1;
			printf("Something executed one cycle\n");
			current->remaining_cycles-=1;
			using_mul++;
		}
		else if(current->read == 0){
			current->read = 1;
			printf("Read instructions\n");
		}
	}

	else if(current->executing == 1){
		current->remaining_cycles-=1;
		printf("Something executed one cycle\n");
	}

	if(current->next == NULL)
		break;
	else
		current = current->next;
}


current = beginning_of_ls;

while(current != NULL){
	if(current->iready == 0){
		if(current->ires->complete == 1){
			current->iready = 1;
			current->ires = NULL;
		}
	}
	if(current->jready == 0){
		if(current->jres->complete == 1){
			current->jready = 1;
			current->jres = NULL;
		}
	}

	if(current->iready == 1 && current->jready == 1 && current->executing == 0){
		if(current->read == 1 && using_ls < ls_num && current == beginning_of_ls){
			current->executing = 1;
			printf("ls executed one cycle\n");
			current->remaining_cycles-=1;
			using_ls++;
		}
		else if(current->read == 0){
			current->read = 1;
			printf("Read instructions\n");
		}
	}

	else if(current->remaining_cycles == 0){
		memory_latency->read(current->memloc, callback_function);
		current->executing = 0;
	}

	else if(current->executing == 1){
		current->remaining_cycles-=1;
		printf("ls executed one cycle\n");
	}

	if(current->next == NULL)
		break;
	else
		current = current->next;
}





if((instr_run == instr_count) && (int_empty == 1 || beginning_of_int == NULL) && (div_empty == 1 || beginning_of_div == NULL) && (mul_empty == 1 || beginning_of_mul == NULL) && (ls_empty == 1 || beginning_of_ls == NULL)){
	primaryComponentOKToEndSim();
	unregisterExit();
	return true;
}

	if(instr_run<instr_count){
		instructions++;
		uint16_t instr = programArray[instr_run++];

		//add/sub/and/nor
		if((instr & 0xF800) == 0x0000 || (instr & 0xF800) == 0x0800 || (instr & 0xF800) == 0x1000 || (instr & 0xF800) == 0x1800){
			if(in_int < int_res){
					nodep next = new node;
					if(beginning_of_int == NULL){
						beginning_of_int = next;
						end_of_int = next;
						next->next = NULL;
						next->prev = NULL;
					}
					else{
						end_of_int->next = next;
						next->prev = end_of_int;
						next->next = NULL;
						end_of_int = next;
					}
					if(reg_file[instr>>5 & 0x0007] != NULL){
						next->ires = reg_file[instr>>5 & 0x0007];
						next->iready = 0;
					}
					else{
						next->iready = 1;
						}
					if(reg_file[instr>>2 & 0x0007] != NULL){
						next->jres = reg_file[instr>>2 & 0x0007];
						next->iready = 0;
					}
					else{
						next->jready = 1;
					}

					reg_file[instr>>8 & 0x0007] = next;
					next->remaining_cycles = int_lat;
					next->executing=0;
					next->complete=0;
					next->read = 0;
					next->destreg = (instr>>8 & 0x0007);
					int_empty = 0;
					in_int++;
					printf("Add/sub/and/nor issued\n");
			}
				else{
					instr_run--;
				}
		}

//Liz,lis,lui
		else if((instr & 0xF800) == 0x8000 || (instr & 0xF800) == 0x8800 || (instr & 0xF800) == 0x9000){
			if(in_int < int_res){
					nodep next = new node;
					if(beginning_of_int == NULL){
						beginning_of_int = next;
						end_of_int = next;
						next->next = NULL;
						next->prev = NULL;
					}
					else{
						end_of_int->next = next;
						next->prev = end_of_int;
						next->next = NULL;
						end_of_int = next;
					}
						next->iready = 1;
						next->jready = 1;

					reg_file[instr>>8 & 0x0007] = next;
					next->remaining_cycles = int_lat;
					next->executing=0;
					next->complete=0;
					next->read = 0;
					next->destreg = (instr>>8 & 0x0007);
					int_empty = 0;
					in_int++;
					printf("Lis/liz/lui issued\n");
			}
				else{
					instr_run--;
				}
		}

		//Halt or put
		else if((instr & 0xF800) == 0x7000 || (instr & 0xF800) == 0x6800){
			if(in_int < int_res){
					nodep next = new node;
					if(beginning_of_int == NULL){
						beginning_of_int = next;
						end_of_int = next;
						next->next = NULL;
						next->prev = NULL;
					}
					else{
						end_of_int->next = next;
						next->prev = end_of_int;
						next->next = NULL;
						end_of_int = next;
					}
						next->iready = 1;
						next->jready = 1;

					next->remaining_cycles = int_lat;
					next->executing=0;
					next->complete=0;
					next->read = 0;
					next->destreg = -1;
					int_empty = 0;
					in_int++;
					printf("Halt/put issued\n");
			}
				else{
					instr_run--;
				}
		}

		//Div/mod/exp instr
		else if((instr & 0xF800) == 0x2000 || (instr & 0xF800) == 0x3800 || (instr & 0xF800) == 0x3000){
			if(in_div < div_res){
					nodep next = new node;
					if(beginning_of_div == NULL){
						beginning_of_div = next;
						end_of_div = next;
						next->next = NULL;
						next->prev = NULL;
					}
					else{
						end_of_div->next = next;
						next->prev = end_of_div;
						next->next = NULL;
						end_of_div = next;
					}

					if(reg_file[instr>>5 & 0x0007] != NULL){
						next->ires = reg_file[instr>>5 & 0x0007];
						next->iready = 0;
					}
					else{
						next->iready = 1;
						}
					if(reg_file[instr>>2 & 0x0007] != NULL){
						next->jres = reg_file[instr>>2 & 0x0007];
						next->iready = 0;
					}
					else{
						next->jready = 1;
					}

					reg_file[instr>>8 & 0x0007] = next;
					next->remaining_cycles = div_lat;
					next->executing=0;
					next->complete=0;
					next->read = 0;
					next->destreg = (instr>>8 & 0x0007);
					div_empty = 0;
					in_div++;
					printf("div/mod/exp issued\n");
			}
				else{
					instr_run--;
				}
		}

		//Mult instr
		else if((instr & 0xF800) == 0x2800){
			if(in_mul < mul_res){
					nodep next = new node;
					if(beginning_of_mul == NULL){
						beginning_of_mul = next;
						end_of_mul = next;
						next->next = NULL;
						next->prev = NULL;
					}
					else{
						end_of_mul->next = next;
						next->prev = end_of_mul;
						next->next = NULL;
						end_of_mul = next;
					}

					if(reg_file[instr>>5 & 0x0007] != NULL){
						next->ires = reg_file[instr>>5 & 0x0007];
						next->iready = 0;
					}
					else{
						next->iready = 1;
						}
					if(reg_file[instr>>2 & 0x0007] != NULL){
						next->jres = reg_file[instr>>2 & 0x0007];
						next->iready = 0;
					}
					else{
						next->jready = 1;
					}

					reg_file[instr>>8 & 0x0007] = next;
					next->remaining_cycles = mul_lat;
					next->executing=0;
					next->complete=0;
					next->read = 0;
					next->destreg = (instr>>8 & 0x0007);
					mul_empty = 0;
					in_mul++;
					printf("div/mod/exp issued\n");
			}
				else{
					instr_run--;
				}
		}

		//LW
		else if((instr & 0xF800) == 0x4000){

			if(in_ls < ls_res){
					nodep next = new node;
					if(beginning_of_ls == NULL){
						beginning_of_ls = next;
						next->next = NULL;
					}
					else{
						nodep current = beginning_of_ls;
						while(current->next != NULL)
							current = current->next;

						current->next = next;
						next->next = NULL;
					}


					if(reg_file[instr>>5 & 0x0007] != NULL){
						next->ires = reg_file[instr>>5 & 0x0007];
						next->iready = 0;
					}
					else{
						next->iready = 1;
						}
					next->jready = 1;

					reg_file[instr>>8 & 0x0007] = next;
					next->remaining_cycles = ls_lat;
					next->executing=0;
					next->complete=0;
					next->read = 0;
					next->destreg = (instr>>8 & 0x0007);
					next->memloc = data_memory[instr_run-1];
					ls_empty = 0;
					in_ls++;
					printf("lw issued\n");
			}
				else{
					instr_run--;
				}
					std::function<void(uarch_t, uarch_t)> callback_function = [this](uarch_t request_id, uarch_t addr)
			{
				this->busy=false;
			};
			memory_latency->read(1, callback_function);
			registers[instr>>8 & 0x0007] = data_memory[registers[instr>>5 & 0x0007]];
			busy=true;
		}

		//SW
		else if((instr & 0xF800) == 0x4800){
			counts[9]++;

					std::function<void(uarch_t, uarch_t)> callback_function = [this](uarch_t request_id, uarch_t addr)
			{
				this->busy=false;
			};
			memory_latency->read(1, callback_function);
			busy=true;
		}
	}
	in_int-=to_dec_int;
	using_int-=to_dec_int;

	in_div-=to_dec_div;
	using_div-=to_dec_div;

	in_mul-=to_dec_mul;
	using_mul-=to_dec_mul;

	in_ls-=to_dec_ls;
	using_ls-=to_dec_ls;

	to_dec_ls = 0;

return false;
}


void core::memory_callback(SimpleMem::Request *ev)
{
if(memory_latency)
{
	memory_latency->callback(ev);
}
}

}
}
