/* 046267 Computer Architecture - Winter 20/21 - HW #4 */

#include "core_api.h"
#include "sim_api.h"
#include "list"
#include "algorithm"

class Thread{
public:
    int IP;
    int ThreadID;
    int SleptAt;
    bool IsLoading;
    tcontext regState;

    Thread(int tid = -1) {
        IP = 0;
        ThreadID = tid;
        SleptAt = -1;
        IsLoading = false;
        for(int i = 0; i < 8; i++)
            regState.reg[i] = 0;
    }
    /* this is the main functions that deals with the implementation of the different instructions specified in this HW and what
     * needs to be done after each one of them.*/
    void DoSomthing(Instruction inst,int StopCyclesAt = -1){
        IP++;
        switch (inst.opcode) {
            case CMD_NOP:
                SleptAt = StopCyclesAt;
                return;
            case CMD_ADD:
                regState.reg[inst.dst_index] = regState.reg[inst.src1_index] + regState.reg[inst.src2_index_imm];
                SleptAt = StopCyclesAt;
                return;
            case CMD_SUB:
                regState.reg[inst.dst_index] = regState.reg[inst.src1_index] - regState.reg[inst.src2_index_imm];
                SleptAt = StopCyclesAt;
                return;
            case CMD_ADDI:
                regState.reg[inst.dst_index] = regState.reg[inst.src1_index] + inst.src2_index_imm;
                SleptAt = StopCyclesAt;
                return;
            case CMD_SUBI:
                regState.reg[inst.dst_index] = regState.reg[inst.src1_index] - inst.src2_index_imm;
                SleptAt = StopCyclesAt;
                return;
            case CMD_LOAD:
                int src2;
                if(inst.isSrc2Imm)
                    src2 = inst.src2_index_imm;
                else
                    src2 = regState.reg[inst.src2_index_imm];
                SIM_MemDataRead(regState.reg[inst.src1_index] + src2,&(regState.reg[inst.dst_index]));
                SleptAt = StopCyclesAt;
                return;
            case CMD_STORE:
                int src2ish;
                if(inst.isSrc2Imm)
                    src2ish = inst.src2_index_imm;
                else
                    src2ish = regState.reg[inst.src2_index_imm];
                SIM_MemDataWrite(regState.reg[inst.dst_index] + src2ish,regState.reg[inst.src1_index]);
                SleptAt = StopCyclesAt;
                return;
            case CMD_HALT:
                return;
        }
    }

};
/* class for MultiThread implementation*/
class MTManger{
public:
    Thread* CurrThread;
    int ThreadNum;
    int RunTime;
    int InstructionCount;
    std::list<Thread*> Threads; // list of overall Threads in program.
    std::list<Thread*> ThreadPool; // list of current unhalted Threads.

    MTManger(){
        ThreadNum = SIM_GetThreadsNum();
        RunTime = 0;
        InstructionCount = 0;
        for(int i = 0; i < ThreadNum; i++){
            auto* pthread = new Thread(i);
            ThreadPool.push_back(pthread);
            Threads.push_back(pthread);
        }
    }

    ~MTManger(){
        for(auto & Thread : Threads){
            delete Thread;
        }
    }
    // returns next Thread in line and advances the queue.
    Thread* GetThread(){
        Thread* OLD;
        OLD = CurrThread;
        auto it = ++std::find(ThreadPool.begin(), ThreadPool.end(), CurrThread);
        if(it == ThreadPool.end()) {
            if (ThreadPool.empty())
                return nullptr;
            CurrThread = ThreadPool.front();
            return OLD;
        }
        CurrThread = *it;
        return OLD;
    }
    /* used to check if all Threads are busy.*/
    bool AreTheySleeping(){
        for(auto & Thread : ThreadPool){
            if(Thread->SleptAt != -1 && Thread->IsLoading && Thread->SleptAt + SIM_GetLoadLat() > RunTime)
                continue;
            if(Thread->SleptAt != -1 && !Thread->IsLoading && Thread->SleptAt + SIM_GetStoreLat() > RunTime)
                continue;
            return false;
        }
        return true;
    }

    void MAINBOI(bool isBlocked){
        CurrThread = ThreadPool.front();
        Thread* RunningThread,*PrevThread = nullptr;
        bool wasAsleep = false;
        while(!(ThreadPool.empty())){ // atleast 1 Thread is still running/ not halted.

            Instruction Inst;

            if(AreTheySleeping()){
                RunTime++;
                wasAsleep = true;
                continue;
            }
            if(PrevThread && isBlocked){ // revert the queue advancement for certain instructions.
                SIM_MemInstRead(PrevThread->IP - 1,&Inst,PrevThread->ThreadID);
                if(Inst.opcode != CMD_HALT && Inst.opcode != CMD_STORE && Inst.opcode != CMD_LOAD)
                    CurrThread = PrevThread;
            }
            if(!wasAsleep|| ThreadPool.size() == 1 || !isBlocked) // context switch for Fine-Grained or if in previous Cycle not all threads were busy.
                RunningThread = GetThread();

            SIM_MemInstRead(RunningThread->IP,&Inst,RunningThread->ThreadID);

            wasAsleep = false; // set WasAsleep to false for next iteration.

            if(RunningThread->SleptAt != -1 && RunningThread->IsLoading && RunningThread->SleptAt + SIM_GetLoadLat() > RunTime && SIM_GetLoadLat() != 1)
                continue; //in Store inst, if next Thread in line is busy, move to next Thread in next iteration.
            if(RunningThread->SleptAt != -1 && !RunningThread->IsLoading && RunningThread->SleptAt + SIM_GetStoreLat() > RunTime && SIM_GetStoreLat() != 1)
                continue; //in Load inst, if next Thread in line is busy, move to next Thread in next iteration.
            if(isBlocked && RunTime != 0 && PrevThread != RunningThread)
                RunTime += SIM_GetSwitchCycles(); // count Context Switch cycles when switching. specifically don't if we're at first instruction.

            PrevThread = RunningThread;

            if(Inst.opcode == CMD_NOP){
                RunningThread->DoSomthing(Inst);
                InstructionCount++;
                RunTime++;
                continue;
            }

            if(Inst.opcode == CMD_HALT){
                auto it = std::find(ThreadPool.begin(), ThreadPool.end(), RunningThread);
                ThreadPool.erase(it); // remove from queue.
                InstructionCount++;
                RunTime++;
                PrevThread = nullptr;
                continue;
            }
            if(Inst.opcode == CMD_LOAD){
                RunTime++;
                InstructionCount++;
                RunningThread->IsLoading = true;
                RunningThread->DoSomthing(Inst,RunTime);
                continue;
            }
            if(Inst.opcode == CMD_STORE){
                RunTime++;
                InstructionCount++;
                RunningThread->IsLoading = false;
                RunningThread->DoSomthing(Inst,RunTime);
                continue;
            }
            /* reach this code segment for non memory related nor HALT,NOP instructions*/
            InstructionCount++;
            RunTime++;
            RunningThread->DoSomthing(Inst);
        }
    }
};

MTManger* BlockedMT,*FineGrainedMT;

void CORE_BlockedMT() {
    BlockedMT = new MTManger();
    BlockedMT->MAINBOI(true);
}

void CORE_FinegrainedMT() {
    FineGrainedMT = new MTManger();
    FineGrainedMT->MAINBOI(false);
}

double CORE_BlockedMT_CPI(){
    double CLKS = BlockedMT->RunTime;
    double INSTS = BlockedMT->InstructionCount;
    delete BlockedMT;
    return CLKS/INSTS;
}

double CORE_FinegrainedMT_CPI(){
    double CLKS = FineGrainedMT->RunTime;
    double INSTS = FineGrainedMT->InstructionCount;
    delete FineGrainedMT;
    return CLKS/INSTS;
}

void CORE_BlockedMT_CTX(tcontext* context, int threadid) {
    for(auto Thread : BlockedMT->Threads){
        if(Thread->ThreadID == threadid){
            *(context+threadid) = Thread->regState;
            return;
        }
    }
}

void CORE_FinegrainedMT_CTX(tcontext* context, int threadid) {
    for(auto Thread : FineGrainedMT->Threads){
        if(Thread->ThreadID == threadid){
            *(context+threadid) = Thread->regState;
            return;
        }
    }
}
