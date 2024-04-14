#include <getopt.h>
#include <fstream>
#include <vector>
#include <cstring>
#include <deque>

#define MAX_FRAMES 128
#define MAX_VPAGES 64

using namespace std;

typedef struct frame_t {
    frame_t() : is_assigned(false), pid(-1), vpage(-1), frame_id(-1), is_victim(false) {}

    bool is_assigned;
    int pid;
    int vpage;
    int frame_id;
    bool is_victim;
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
pte_t PAGE_TABLE[MAX_VPAGES];    // page table of the current process
deque<frame_t *> FREE_FRAMES;     // list of free frames
int NUM_FRAMES = 0;              // total number of frames in the frame_table
int NUM_PROCS = 0;               // total number of processes

class Process {
private:
    static int process_count;
    int pid;

public:
    int num_vmas;
    vector<vma_t> vma_list;
    pte_t page_table[MAX_VPAGES]{0};

    Process() {
        pid = Process::process_count;
        Process::process_count++;
        num_vmas = 0;
    }

    [[nodiscard]] int get_pid() const {
        return pid;
    }
};

int Process::process_count = 0; // initializing the static int - process_count

class Pager {
public:
    virtual frame_t *select_victim_frame() = 0;

    virtual ~Pager() = default;
};

class FCFSPager : public Pager {
private:
    int curr_idx;
public:
    FCFSPager() {
        curr_idx = 0;
    }

    frame_t *select_victim_frame() override {
        frame_t *victim = &FRAME_TABLE[curr_idx];
        curr_idx = (curr_idx + 1) % NUM_FRAMES;
        return victim;
    }
};

/**
 * Global variables part 2
 */
int RAND_COUNT = 0;              // total number of random numbers in file
vector<int> RANDVALS;            // initialize a list of random numbers
int OFS = 0;                     // line offset for the random file
Pager *PAGER = nullptr;          // pager instance used in the simulation
vector<Process *> PROCS;         // initialize a list of processes
Process *CURR_PROC = nullptr;    // pointer to the current running process
deque<ins_t> INSTRUCTIONS;       // list of instructions
int INS_COUNTER = 0;             // instruction counter

/**
 * List of option flags
 */
bool VERBOSE = false;
bool SHOW_PAGE_TABLE = false;
bool SHOW_FRAME_TABLE = false;
bool SHOW_STATS = false;
bool SHOW_CURR_PT = false;
bool SHOW_PROCESS_PT = false;
bool SHOW_CURR_FT = false;
bool SHOW_AGING_INFO = false;

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
 * Get random number from randvals
 * @param - burst - the corresponding CPU or IO burst
 *
 * @returns - random value in the range of 1,..,burst
 */
int get_random(int burst) {
    int offset = OFS % RAND_COUNT;
    int random = 1 + (RANDVALS[offset] % burst);
    OFS++;
    return random;
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
            return new FCFSPager();
        case 'c':
            return new FCFSPager();
        case 'e':
            return new FCFSPager();
        case 'a':
            return new FCFSPager();
        case 'w':
            return new FCFSPager();
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

    // TODO: calculate context switch time
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

    // TODO: make unmap calculations

    if (VERBOSE) printf(" UNMAP %d:%d\n", old_pid, old_vpage);
    if (old_pte->is_modified) {
        if (old_pte->is_file_mapped) {
            if (VERBOSE) printf(" FOUT\n");
        } else {
            old_pte->is_paged_out = true;
            if (VERBOSE)printf(" OUT\n");
        }
        old_pte->is_modified = false;
    }

    // ???
    old_pte->is_present = false;
}

/**
 * Handle Load/Store Operations
 * @param op
 * @param vpage
 */
void handle_load_store(char op, int vpage) {
    pte_t *pte = &(CURR_PROC->page_table[vpage]);
    if (!pte->is_present) {
        // TODO: make it valid/present
        bool is_valid = check_validity_and_cache_details(vpage);
        if (!is_valid) {
            if (VERBOSE)
                printf(" SEGV\n");

            // TODO: handle segv calculations

            return;
        }

        frame_t *new_frame = get_frame();

        if (new_frame->is_victim) {
            unmap_victim_frame(new_frame);
        }

        if (pte->is_file_mapped) {
            if (VERBOSE) printf(" FIN\n");
        } else if (pte->is_paged_out) {
            if (VERBOSE) printf(" IN\n");
        } else {
            if (VERBOSE) printf(" ZERO\n");
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
    }

    pte->is_referenced = 1;
    // TODO: make reference calculations

    if (op == 'w') {
        if (pte->is_write_protected) {
            if (VERBOSE) printf(" SEGPROT\n");

            // TODO: make segprot calculations
        } else {
            pte->is_modified = 1;
        }
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
                continue;
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
 * Print the final desired output based on global flags
 */
void print_output() {
    if (SHOW_PAGE_TABLE)
        print_page_tables();
    if (SHOW_FRAME_TABLE)
        print_frame_table();
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
