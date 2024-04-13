#include <getopt.h>
#include <fstream>
#include <vector>
#include <cstring>

#define MAX_FRAMES 128
#define MAX_VPAGES 64

using namespace std;

typedef struct {

} frame_t;

typedef struct {
    unsigned int is_assigned_to_vma: 1;     // is the v_page a part of one of the VMAs
    unsigned int is_valid: 1;               // is the v_page a valid/present page
    unsigned int is_referenced: 1;          // is the v_page referenced
    unsigned int is_modified: 1;            // is the v_page modified/written
    unsigned int is_write_protected: 1;     // is the corresponding VMA write protected
    unsigned int is_paged_out: 1;           // is the corresponding VMA mapped to a file
} pte_t;

typedef struct {
    unsigned int start_page: 6;             // max value is 2^6 = 64 (max no. of vpages)
    unsigned int end_page: 6;               // max value is 2^6 = 64 (max no. of vpages)
    unsigned int is_write_protected: 1;     // is VMA write protected
    unsigned int is_file_mapped: 1;         // is VMA file mapped
} vma_t;

typedef struct {
    char op;  // opcode
    int addr; // address
} ins_t;

class Process {
private:
    static int process_count;
    int pid;

public:
    int num_vmas;
    vector<vma_t> vma_list;
    pte_t page_table[MAX_VPAGES]{0};

    Process() {
        pid = process_count;
        process_count++;
        num_vmas = 0;
    }

    [[nodiscard]] int get_pid() const {
        return pid;
    }
};
int Process::process_count = 0; //initializing the static int - process_count

class Pager {
public:
    virtual frame_t *select_victim_frame() = 0;

    virtual ~Pager() = default;
};

class FCFSPager : public Pager {
public:
    frame_t *select_victim_frame() override {
        return new frame_t();
    }
};

/**
 * Global variables
 */
frame_t frame_table[MAX_FRAMES];
pte_t page_table[MAX_VPAGES];

int RAND_COUNT = 0;             // total number of random numbers in file
vector<int> RANDVALS;           // initialize a list of random numbers
int OFS = 0;                    // line offset for the random file
int NUM_FRAMES = 0;             // total number of frames in the frame_table
int NUM_PROCS = 0;              // total number of processes
Pager *PAGER = nullptr;         // pager instance used in the simulation
vector<Process *> PROCS;        // initialize a list of processes
Process *CURR_PROC = nullptr;   // pointer to the current running process
vector<ins_t> INSTRUCTIONS;     // list of instructions

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

frame_t *get_frame() {
    return new frame_t();
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
            printf("Unknown Replacement Algorithm: %c", args[0]);
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

        for(int j = 0; j < process->num_vmas; j++) {
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
    while(getline(input_file, line)) {
        if(line[0] == '#') continue;

        char *buffer = new char[line.length() + 1];
        strcpy(buffer, line.c_str());

        ins_t ins;
        sscanf(buffer, "%c %d", &ins.op, &ins.addr);
        INSTRUCTIONS.push_back(ins);

        delete[] buffer;
    }
}

/**
 * Debug function to print tokens/output
 */
[[maybe_unused]] void print_output() {
    printf("NUM_FRAMES = %d\n", NUM_FRAMES);
    printf("NUM_PROCESSES = %d\n", NUM_PROCS);
    for(Process *p : PROCS) {
        printf("PROCESS %d\n", p->get_pid());
        printf("\tNUM VMAS = %d\n", p->num_vmas);
        for(vma_t vma : p->vma_list) {
            printf("\t\t %d : %d : %d : %d\n", vma.start_page, vma.end_page, vma.is_write_protected, vma.is_file_mapped);
        }
    }
    printf("INSTRUCTIONS\n");
    for(ins_t ins : INSTRUCTIONS) {
        printf("\t%c : %d\n", ins.op, ins.addr);
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
    garbage_collection();
}
