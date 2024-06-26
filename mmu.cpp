#include <getopt.h>
#include <fstream>
#include <vector>
#include <cstring>
#include <deque>

#define MAX_FRAMES 128
#define MAX_VPAGES 64
#define NRU_RESET_COUNT 48
#define WORKING_SET_TAU 49

#define CTX_SWITCH_TIME 130
#define LD_ST_TIME 1
#define PROC_EXIT_TIME 1230
#define MAPS_TIME 350
#define UNMAPS_TIME 410
#define INS_TIME 3200
#define OUTS_TIME 2750
#define FINS_TIME 2350
#define FOUTS_TIME 2800
#define ZEROS_TIME 150
#define SEGV_TIME 440
#define SEGPROT_TIME 410

using namespace std;

typedef struct frame_t {
    frame_t() : is_assigned(false), pid(-1), vpage(-1), frame_id(-1), is_victim(false), age(0) {}

    bool is_assigned;
    int pid;
    int vpage;
    int frame_id;
    bool is_victim;
    unsigned long int age;
} frame_t;

typedef struct {
    unsigned int is_present: 1;         // is the v_page a valid/present page
    unsigned int is_referenced: 1;      // is the v_page referenced
    unsigned int is_modified: 1;        // is the v_page modified/written
    unsigned int is_write_protected: 1; // is the corresponding VMA write protected
    unsigned int is_paged_out: 1;       // is the v_page paged_out

    unsigned int is_assigned_to_vma: 1; // is the v_page a part of one of the VMAs
    unsigned int is_file_mapped: 1;     // is the corresponding VMA mapped to a file

    unsigned int frame_num: 7; // frame_num can be at max 128 = 2^7 => 7 bits
} pte_t;

typedef struct {
    unsigned int start_page: 6;         // max value is 2^6 = 64 (max no. of vpages)
    unsigned int end_page: 6;           // max value is 2^6 = 64 (max no. of vpages)
    unsigned int is_write_protected: 1; // is VMA write protected
    unsigned int is_file_mapped: 1;     // is VMA file mapped
} vma_t;

typedef struct {
    char op;  // opcode
    int addr; // address
} ins_t;

/**
 * Global variables part 1
 */
frame_t FRAME_TABLE[MAX_FRAMES]; // global frame table to keep track of all the frames in the physical memory
deque<frame_t *> FREE_FRAMES;    // list of free frames
int NUM_FRAMES = 0;              // total number of frames in the frame_table
int NUM_PROCS = 0;               // total number of
int RAND_COUNT = 0;              // total number of random numbers in file
vector<int> RANDVALS;            // initialize a list of random numbers
int OFS = 0;                     // line offset for the random file

/**
 * List of option flags
 */
bool VERBOSE = false;
bool SHOW_PAGE_TABLE = false;
bool SHOW_FRAME_TABLE = false;
bool SHOW_STATS = false;
bool SHOW_AGING_INFO = false;
[[maybe_unused]] bool SHOW_CURR_PT = false;
[[maybe_unused]] bool SHOW_PROCESS_PT = false;
[[maybe_unused]] bool SHOW_CURR_FT = false;

unsigned long long int INS_COUNTER = 0;     // instruction counter
unsigned long long int CTX_SWITCHES = 0;    // total context switches
unsigned long long int PROC_EXITS = 0;      // total process exits
unsigned long long int COST = 0;            // total cost

class Process {
private:
    static int process_count;
    int pid;

public:
    int num_vmas;
    vector<vma_t> vma_list;
    pte_t page_table[MAX_VPAGES]{0};

    unsigned long long unmaps;
    unsigned long long maps;
    unsigned long long ins;
    unsigned long long outs;
    unsigned long long fins;
    unsigned long long fouts;
    unsigned long long zeros;
    unsigned long long segv;
    unsigned long long segprot;

    Process() {
        pid = Process::process_count;
        Process::process_count++;
        num_vmas = 0;
        unmaps = maps = ins = outs = fins = fouts = zeros = segv = segprot = 0;
    }

    [[nodiscard]] int get_pid() const {
        return pid;
    }
};

int Process::process_count = 0; // initializing the static int - process_count
vector<Process *> PROCS;        // initialize a list of processes

/**
 * Helper function to get the Page table entry given the frame_id
 *
 * @param id - frame id (can be used to access into the FRAME_TABLE)
 * @return pte_t* - the corresponding page table entry
 */
pte_t *reverse_map(int id) {
    int pid = FRAME_TABLE[id].pid;
    int vpage = FRAME_TABLE[id].vpage;
    return &PROCS[pid]->page_table[vpage];
}

/**
 * Get random number from randvals
 * @param - burst - the corresponding CPU or IO burst
 *
 * @returns - random value in the range of 1,..,burst
 */
int get_random() {
    int offset = OFS % RAND_COUNT;
    int random = RANDVALS[offset] % NUM_FRAMES;
    OFS++;
    return random;
}

class Pager {
public:
    virtual frame_t *select_victim_frame() = 0;

    virtual void reset_age(unsigned int frame_id) = 0;

    virtual ~Pager() = default;
};

class FCFSPager : public Pager {
private:
    int curr_idx;
public:
    FCFSPager() {
        curr_idx = 0;
    }

    void reset_age(unsigned int frame_id) override {}

    frame_t *select_victim_frame() override {
        frame_t *victim = &FRAME_TABLE[curr_idx];
        if (SHOW_AGING_INFO) printf("ASELECT %d\n", curr_idx);
        curr_idx = (curr_idx + 1) % NUM_FRAMES;
        return victim;
    }
};

class RandomPager : public Pager {
public:
    frame_t *select_victim_frame() override {
        int index = get_random();
        return &FRAME_TABLE[index];
    }

    void reset_age(unsigned int frame_id) override {}
};

class ClockPager : public Pager {
private:
    int clock_idx;
public:
    ClockPager() {
        clock_idx = 0;
    }

    frame_t *select_victim_frame() override {
        int start = clock_idx;
        int count = 0;
        pte_t *clock_pte = reverse_map(clock_idx);
        count++;
        while (clock_pte->is_referenced) {
            clock_pte->is_referenced = false;
            clock_idx = (clock_idx + 1) % NUM_FRAMES;
            clock_pte = reverse_map(clock_idx);
            count++;
        }

        if (SHOW_AGING_INFO) printf("ASELECT %d %d\n", start, count);
        frame_t *victim = &FRAME_TABLE[clock_idx];
        clock_idx = (clock_idx + 1) % NUM_FRAMES;
        return victim;
    }

    void reset_age(unsigned int frame_id) override {}
};

class NRUPager : public Pager {
private:
    int reset_cycle;
    unsigned long long int last_reset;
    int hand;

public:
    NRUPager() : hand(0), last_reset(0), reset_cycle(NRU_RESET_COUNT) {}

    static int get_class_index(pte_t *pte) {
        int r = pte->is_referenced;
        int m = pte->is_modified;
        return 2 * r + m;
    }

    frame_t *select_victim_frame() override {
        int class_frame_map[4] = {-1, -1, -1, -1};

        // variables for ASELECT
        int start = hand;
        bool reset = INS_COUNTER >= (last_reset + reset_cycle);
        int lowest_class_found = -1;
        int victim_frame_id = -1;
        int scan_count = 0;

        for (int i = 0; i < NUM_FRAMES; i++) {
            scan_count++;
            int idx = (hand + i) % NUM_FRAMES;
            pte_t *pte = reverse_map(idx);
            int page_type = get_class_index(pte);

            if (class_frame_map[page_type] == -1) {
                class_frame_map[page_type] = idx;
            }

            if (page_type == 0 && !reset) {
                victim_frame_id = idx;
                lowest_class_found = 0;
                break;
            }

            if (reset) {
                pte->is_referenced = false;
            }
        }

        if (lowest_class_found == -1) {
            for (int i = 0; i < 4; i++) {
                if (class_frame_map[i] != -1) {
                    victim_frame_id = class_frame_map[i];
                    lowest_class_found = i;
                    break;
                }
            }
        }

        frame_t *victim = &FRAME_TABLE[victim_frame_id];
        if (SHOW_AGING_INFO) {
            printf("ASELECT: hand=%2d %d | %d %2d %2d\n", start, reset, lowest_class_found, victim_frame_id,
                   scan_count);
        }
        hand = (victim_frame_id + 1) % NUM_FRAMES;
        if (reset) {
            last_reset = INS_COUNTER;
        }
        return victim;
    }

    void reset_age(unsigned int frame_id) override {}
};

class AgingPager : public Pager {
private:
    int hand;
public:
    AgingPager() : hand(0) {}

    frame_t *select_victim_frame() override {
        int start_idx = hand;
        int min_age_idx = hand;

        for (int i = 0; i < NUM_FRAMES; i++) {
            int idx = (hand + i) % NUM_FRAMES;

            pte_t *pte = reverse_map(idx);
            frame_t *frame = &FRAME_TABLE[idx];

            // shift right by 1 (divide by 2)
            frame->age >>= 1;
            if (pte->is_referenced) {
                // add the reference bit to left of age
                frame->age |= 0x80000000;
                // reset R bit
                pte->is_referenced = false;
            }
            min_age_idx = FRAME_TABLE[idx].age < FRAME_TABLE[min_age_idx].age ? idx : min_age_idx;
        }

        if (SHOW_AGING_INFO) {
            int end_idx = (start_idx + NUM_FRAMES - 1) % NUM_FRAMES;
            printf("ASELECT %d-%d |", start_idx, end_idx);

            for (int i = 0; i < NUM_FRAMES; i++) {
                int idx = (start_idx + i) % NUM_FRAMES;
                frame_t *frame = &FRAME_TABLE[idx];
                printf(" %d:%lx", idx, frame->age);
            }

            printf(" | %d\n", min_age_idx);
        }

        frame_t *victim = &FRAME_TABLE[min_age_idx];
        hand = (min_age_idx + 1) % NUM_FRAMES;
        return victim;
    }

    void reset_age(unsigned int frame_id) override {
        frame_t *frame = &FRAME_TABLE[frame_id];
        frame->age = 0;
    }
};

class WorkingSetPager : public Pager {
private:
    int hand;
    int tau;
public:
    WorkingSetPager() : hand(0), tau(WORKING_SET_TAU) {}

    frame_t *select_victim_frame() override {
        int start_idx = hand;
        int oldest_idx = hand;

        if (SHOW_AGING_INFO) {
            int end_idx = (start_idx + NUM_FRAMES - 1) % NUM_FRAMES;
            printf("ASELECT %d-%d |", start_idx, end_idx);
        }

        int count = 0;
        for (int i = 0; i < NUM_FRAMES; i++) {
            count++;
            int idx = (hand + i) % NUM_FRAMES;

            pte_t *pte = reverse_map(idx);
            frame_t *frame = &FRAME_TABLE[idx];

            if (SHOW_AGING_INFO) {
                printf(" %d(%d %d:%d %lu)", idx, pte->is_referenced, frame->pid, frame->vpage, frame->age - 1);
            }

            bool is_old = INS_COUNTER > (frame->age + tau);
            if (is_old && !pte->is_referenced) {
                if (SHOW_AGING_INFO) printf(" STOP(%d)", count);
                oldest_idx = idx;
                break;
            }

            if (pte->is_referenced) {
                frame->age = INS_COUNTER;
                pte->is_referenced = false;
            }

            oldest_idx = FRAME_TABLE[idx].age < FRAME_TABLE[oldest_idx].age ? idx : oldest_idx;
        }

        if (SHOW_AGING_INFO) {
            printf(" | %d\n", oldest_idx);
        }

        hand = (oldest_idx + 1) % NUM_FRAMES;
        return &FRAME_TABLE[oldest_idx];
    }

    void reset_age(unsigned int frame_id) override {
        frame_t *frame = &FRAME_TABLE[frame_id];
        frame->age = INS_COUNTER;
    }
};

/**
 * Global variables part 2
 */
Pager *PAGER = nullptr;          // pager instance used in the simulation
Process *CURR_PROC = nullptr;    // pointer to the current running process
deque<ins_t> INSTRUCTIONS;       // list of instructions

/**
 * Helper functions
 */

/**
 * Initialize frames for the FRAME_TABLE
 */
void initialize_frames() {
    for (int i = 0; i < NUM_FRAMES; i++) {
        frame_t *frame = &FRAME_TABLE[i];
        frame->frame_id = i;

        FREE_FRAMES.push_back(frame);
    }
}

/**
 * Allocate frame from free list
 * @return
 */
frame_t *allocate_frame_from_free_list() {
    if (FREE_FRAMES.empty()) return nullptr;
    frame_t *free_frame = FREE_FRAMES.front();
    FREE_FRAMES.pop_front();
    return free_frame;
}

/**
 * Get a frame from the frame table
 * @return frame to be associated with the virtual page
 */
frame_t *get_frame() {
    frame_t *frame = allocate_frame_from_free_list();
    if (frame == nullptr)
        frame = PAGER->select_victim_frame();
    return frame;
}

/**
 * Get the appropriate pager based on the arguments
 * @param - args - string that needs to parsed to fetch the pager type
 *
 * @returns - the correct Pager based on the arguments
 */
Pager *getPager(char *args) {
    switch (args[0]) {
        case 'f':
            return new FCFSPager();
        case 'r':
            return new RandomPager();
        case 'c':
            return new ClockPager();
        case 'e':
            return new NRUPager();
        case 'a':
            return new AgingPager();
        case 'w':
            return new WorkingSetPager();
        default:
            printf("Unknown Replacement Algorithm: %c\n", args[0]);
            exit(1);
    }
}

/**
 * Set the required output options from the arguments
 * @param - args - string that needs to parsed to fetch all the options that need to be set
 *
 */
void set_options(char *args) {
    char *ch = args;

    while (*ch != '\0') {
        switch (*ch) {
            case 'O':
                VERBOSE = true;
                break;
            case 'P':
                SHOW_PAGE_TABLE = true;
                break;
            case 'F':
                SHOW_FRAME_TABLE = true;
                break;
            case 'S':
                SHOW_STATS = true;
                break;
            case 'x':
                SHOW_CURR_PT = true;
                break;
            case 'y':
                SHOW_PROCESS_PT = true;
                break;
            case 'f':
                SHOW_CURR_FT = true;
                break;
            case 'a':
                SHOW_AGING_INFO = true;
                break;
            default:
                printf("Unknown output option: <%c>\n", *ch);
                exit(1);
        }
        ch++;
    }
}

/**
 * Set the frame_table size from the arguments
 * @param - args - string that needs to parsed to fetch the frame_table size
 *
 */
void set_num_frames(char *args) {
    int count = atoi(args);
    if (count > MAX_FRAMES) {
        printf("sorry max frames supported = %d\n", MAX_FRAMES);
        exit(1);
    }
    NUM_FRAMES = count;
}

/**
 * Read command-line arguments and assign values to global variables
 *
 * @param - argc - total argument count
 * @param - argv - array of arguments
 */
void read_arguments(int argc, char **argv) {
    int option;
    while ((option = getopt(argc, argv, "f:a:o:")) != -1) {
        switch (option) {
            case 'f':
                set_num_frames(optarg);
                break;
            case 'a':
                PAGER = getPager(optarg);
                break;
            case 'o':
                set_options(optarg);
                break;
            default:
                printf("option requires an argument -- %c\n", option);
                printf("illegal option\n");
                exit(1);
        }
    }

    if (argc == optind) {
        printf("inputfile name not supplied\n");
        exit(1);
    }
}

/**
 * Parse the numbers from the random-number file
 *
 * @param - filename - random-number file
 */
void parse_randoms(const char *filename) {
    fstream rand_file;
    rand_file.open(filename, ios::in);

    if (!rand_file.is_open()) {
        printf("Cannot open randomfile <%s>\n", filename);
        exit(1);
    }

    string line;
    getline(rand_file, line);
    RAND_COUNT = stoi(line);

    for (int i = 0; i < RAND_COUNT; i++) {
        getline(rand_file, line);
        RANDVALS.push_back(stoi(line));
    }
}

/**
 * Parse the input file to initialize the program
 *
 * @param string
 */
void load_input(const char *filename) {
    fstream input_file;

    input_file.open(filename, ios::in);

    if (!input_file.is_open()) {
        printf("Cannot open inputfile <%s>\n", filename);
        exit(1);
    }

    string line;

    /**
     * Load Process Information
     */
    getline(input_file, line);
    while (line[0] == '#')
        getline(input_file, line);

    NUM_PROCS = atoi(line.c_str());

    for (int i = 0; i < NUM_PROCS; i++) {
        getline(input_file, line);
        while (line[0] == '#')
            getline(input_file, line);
        auto *process = new Process();
        process->num_vmas = atoi(line.c_str());

        for (int j = 0; j < process->num_vmas; j++) {
            getline(input_file, line);
            while (line[0] == '#')
                getline(input_file, line);

            vma_t vma;
            int sp, ep, wp, fm;
            char *buffer = new char[line.length() + 1];
            strcpy(buffer, line.c_str());
            sscanf(buffer, "%d %d %d %d", &sp, &ep, &wp, &fm);
            vma.start_page = sp;
            vma.end_page = ep;
            vma.is_write_protected = wp;
            vma.is_file_mapped = fm;

            process->vma_list.push_back(vma);

            delete[] buffer;
        }
        PROCS.push_back(process);
    }

    /**
     * Load Instructions
     */
    while (getline(input_file, line)) {
        if (line[0] == '#')
            continue;

        char *buffer = new char[line.length() + 1];
        strcpy(buffer, line.c_str());

        ins_t ins;
        sscanf(buffer, "%c %d", &ins.op, &ins.addr);
        INSTRUCTIONS.push_back(ins);

        delete[] buffer;
    }
}

/**
 * Fetch the next instruction
 *
 * @param opcode - any one of 'c', 'r', 'w', 'e'
 * @param target - virtual page number or process number
 *
 * @return boolean - true if next instruction is present, false if not
 */
bool get_next_instruction(char &opcode, int &target) {
    if (INSTRUCTIONS.empty())
        return false;
    ins_t instruction = INSTRUCTIONS.front();
    opcode = instruction.op;
    target = instruction.addr;
    INSTRUCTIONS.pop_front();
    return true;
}

/**
 * Handle context switch operation
 * @param target - process number
 */
void handle_context_switch(int target) {
    CURR_PROC = PROCS[target];

    CTX_SWITCHES++;
    COST += CTX_SWITCH_TIME;
}

/**
 * Checks if the vpage is valid (present in one of the VMAs)
 * Caches the corresponding VMA values to the page table entry
 *
 * @param vpage - virtual page number
 * @return boolean - true if it is valid, false if not
 */
bool check_validity_and_cache_details(int vpage) {
    pte_t *pte = &(CURR_PROC->page_table[vpage]);
    if (pte->is_assigned_to_vma)
        return true;

    for (int i = 0; i < CURR_PROC->num_vmas; i++) {
        vma_t vma = CURR_PROC->vma_list[i];
        if (vpage >= vma.start_page && vpage <= vma.end_page) {
            pte->is_assigned_to_vma = true;
            pte->is_write_protected = vma.is_write_protected;
            pte->is_file_mapped = vma.is_file_mapped;
            return true;
        }
    }

    return false;
}

/**
 * Unmap the victim frame from its previous vpage association
 */
void unmap_victim_frame(frame_t *victim) {
    int old_vpage = victim->vpage;
    int old_pid = victim->pid;

    pte_t *old_pte = &(PROCS[old_pid]->page_table[old_vpage]);

    old_pte->is_present = false;

    if (VERBOSE) printf(" UNMAP %d:%d\n", old_pid, old_vpage);
    PROCS[old_pid]->unmaps++;
    COST += UNMAPS_TIME;

    if (old_pte->is_modified) {
        if (old_pte->is_file_mapped) {
            if (VERBOSE) printf(" FOUT\n");
            PROCS[old_pid]->fouts++;
            COST += FOUTS_TIME;
        } else {
            old_pte->is_paged_out = true;
            if (VERBOSE)printf(" OUT\n");
            PROCS[old_pid]->outs++;
            COST += OUTS_TIME;
        }
        old_pte->is_modified = false;
    }

    old_pte->is_present = false;
}

/**
 * Handle Load/Store Operations
 * @param op
 * @param vpage
 */
void handle_load_store(char op, int vpage) {

    COST += LD_ST_TIME;

    pte_t *pte = &(CURR_PROC->page_table[vpage]);
    if (!pte->is_present) {
        bool is_valid = check_validity_and_cache_details(vpage);
        if (!is_valid) {
            if (VERBOSE)
                printf(" SEGV\n");

            CURR_PROC->segv++;
            COST += SEGV_TIME;
            return;
        }

        frame_t *new_frame = get_frame();

        if (new_frame->is_victim) {
            unmap_victim_frame(new_frame);
        }

        if (pte->is_file_mapped) {
            if (VERBOSE) printf(" FIN\n");
            CURR_PROC->fins++;
            COST += FINS_TIME;
        } else if (pte->is_paged_out) {
            if (VERBOSE) printf(" IN\n");
            CURR_PROC->ins++;
            COST += INS_TIME;
        } else {
            if (VERBOSE) printf(" ZERO\n");
            CURR_PROC->zeros++;
            COST += ZEROS_TIME;
        }

        // assign new pte details to new frame
        new_frame->is_victim = true;
        new_frame->pid = CURR_PROC->get_pid();
        new_frame->vpage = vpage;
        new_frame->is_assigned = true;


        // assign new frame details to new pte
        pte->is_present = true;
        pte->frame_num = new_frame->frame_id;

        if (VERBOSE) printf(" MAP %d\n", pte->frame_num);
        CURR_PROC->maps++;
        COST += MAPS_TIME;
        PAGER->reset_age(pte->frame_num);
    }

    pte->is_referenced = 1;

    if (op == 'w') {
        if (pte->is_write_protected) {
            if (VERBOSE) printf(" SEGPROT\n");
            CURR_PROC->segprot++;
            COST += SEGPROT_TIME;
        } else {
            pte->is_modified = 1;
        }
    }
}

/**
 * Handle Process Exit Operations
 * @param target - process number
 */
void handle_process_exit(int target) {
    printf("EXIT current process %d\n", target);
    PROC_EXITS++;
    COST += PROC_EXIT_TIME;

    Process *active_process = PROCS[target];

    for (int i = 0; i < MAX_VPAGES; i++) {
        pte_t *pte = &(active_process->page_table[i]);
        if (pte->is_present) {
            frame_t *frame = &FRAME_TABLE[pte->frame_num];
            int pid = frame->pid;
            int vpage = frame->vpage;

            // unmap this frame
            if (VERBOSE) printf(" UNMAP %d:%d\n", pid, vpage);
            PROCS[pid]->unmaps++;
            COST += UNMAPS_TIME;

            // free the frame
            frame->is_assigned = false;
            frame->pid = -1;
            frame->vpage = -1;
            frame->is_victim = false;

            FREE_FRAMES.push_back(frame);

            // clear out the page table entry as well
            if (pte->is_modified && pte->is_file_mapped) {
                if (VERBOSE) printf(" FOUT\n");
                PROCS[pid]->fouts++;
                COST += FOUTS_TIME;
            }
        }
        pte->is_present = pte->is_referenced = pte->is_paged_out = 0;
    }
}

/**
 * Start simulation
 */
void run_simulation() {
    char op = 0;
    int target = 0;
    while (get_next_instruction(op, target)) {
        if (VERBOSE) {
            printf("%d: ==> %c %d\n", INS_COUNTER, op, target);
        }
        INS_COUNTER++;
        switch (op) {
            case 'c':
                handle_context_switch(target);
                break;
            case 'r':
            case 'w':
                handle_load_store(op, target);
                break;
            case 'e':
                handle_process_exit(target);
                break;
            default:
                printf("Incorrect instruction operation <%c>\n", op);
                exit(1);
        }
    }
}

/**
 * Debug function to pretty print the input tokens
 */
[[maybe_unused]] void print_input() {
    printf("NUM_FRAMES = %d\n", NUM_FRAMES);
    printf("NUM_PROCESSES = %d\n", NUM_PROCS);
    for (Process *p: PROCS) {
        printf("PROCESS %d\n", p->get_pid());
        printf("\tNUM VMAS = %d\n", p->num_vmas);
        for (vma_t vma: p->vma_list) {
            printf("\t\t %d : %d : %d : %d\n", vma.start_page, vma.end_page, vma.is_write_protected,
                   vma.is_file_mapped);
        }
    }
    printf("INSTRUCTIONS\n");
    for (ins_t ins: INSTRUCTIONS) {
        printf("\t%c : %d\n", ins.op, ins.addr);
    }
}

/**
 * Print the page table for each process after executing all the instructions
 */
void print_page_tables() {
    for (Process *p: PROCS) {
        printf("PT[%d]: ", p->get_pid());
        for (int i = 0; i < MAX_VPAGES; i++) {
            pte_t entry = p->page_table[i];
            if (entry.is_present) {
                printf("%d:", i);
                entry.is_referenced ? printf("R") : printf("-");
                entry.is_modified ? printf("M") : printf("-");
                entry.is_paged_out ? printf("S") : printf("-");
            } else {
                entry.is_paged_out ? printf("#") : printf("*");
            }
            if (i != MAX_VPAGES - 1) printf(" ");
        }
        printf("\n");
    }
}

/**
 * Print the final value of the frame table after
 */
void print_frame_table() {
    printf("FT: ");
    for (int i = 0; i < NUM_FRAMES; i++) {
        frame_t *frame = &FRAME_TABLE[i];
        frame->is_assigned ? printf("%d:%d", frame->pid, frame->vpage) : printf("*");
        if (i != NUM_FRAMES - 1) printf(" ");
    }
    printf("\n");
}

/**
 * Print the per process stats
 */
void print_per_process_stats() {
    for (Process *proc: PROCS) {
        printf("PROC[%d]: U=%llu M=%llu I=%llu O=%llu FI=%llu FO=%llu Z=%llu SV=%llu SP=%llu\n",
               proc->get_pid(),
               proc->unmaps, proc->maps, proc->ins, proc->outs,
               proc->fins, proc->fouts, proc->zeros,
               proc->segv, proc->segprot);
    }
}

void print_global_stats() {
    printf("TOTALCOST %llu %llu %llu %llu %llu\n",
           INS_COUNTER, CTX_SWITCHES, PROC_EXITS, COST, sizeof(pte_t));
}

/**
 * Print the final desired output based on global flags
 */
void print_output() {
    if (SHOW_PAGE_TABLE)
        print_page_tables();
    if (SHOW_FRAME_TABLE)
        print_frame_table();
    if (SHOW_STATS) {
        print_per_process_stats();
        print_global_stats();
    }
}

void garbage_collection() {
    delete PAGER;

    for (Process *p: PROCS)
        delete p;
}

int main(int argc, char **argv) {
    read_arguments(argc, argv);
    if (argc > optind + 1) {
        parse_randoms(argv[optind + 1]);
    }
    load_input(argv[optind]);
    initialize_frames();
    run_simulation();
    print_output();
    garbage_collection();
}
