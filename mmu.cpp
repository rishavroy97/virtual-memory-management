#include <getopt.h>
#include <fstream>
#include <vector>

#define MAX_FRAMES 128
#define MAX_VPAGES 64

using namespace std;

class Process {
};

typedef struct {

} frame_t;

typedef struct {
    unsigned int is_assigned_to_vma: 1; // is the v_page a part of one of the VMAs
    unsigned int is_valid: 1;           // is the v_page a valid/present page
    unsigned int is_referenced: 1;      // is the v_page referenced
    unsigned int is_modified: 1;        // is the v_page modified/written
    unsigned int is_write_protected: 1; // is the corresponding VMA write protected
    unsigned int is_paged_out: 1;       // is the corresponding VMA mapped to a file
} pte_t;

class Pager {
    virtual frame_t *select_victim_frame() = 0;
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

int RAND_COUNT = 0;                         // total number of random numbers in file
vector<int> RANDVALS;                       // initialize a list of random numbers
int OFS = 0;                                // line offset for the random file
int NUM_FRAMES = 0;                         // total number of frames in the frame_table
Pager *PAGER = nullptr;                     // Pager Instance used in the simulation

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
 * Print error message for incorrect input arguments
 * @param - filename - executable file's name
 */
void print_usage(char *filename) {
    printf("Usage: %s [-v] [-t] [-e] [-p] [-i] [-s sched] inputfile randomfile\n"
           "-v enables verbose\n"
           "-t enables scheduler details\n"
           "-e enables event tracing\n"
           "-p enables E scheduler preemption tracing\n"
           "-i single steps event by event\n",
           filename);
}

/**
 * Get the appropriate pager based on the arguments
 * @param - args - string that needs to parsed to fetch the pager type
 *
 * @returns - the correct Pager based on the arguments
 */
Pager *getPager(char *args) {
    switch (args[0]) {
        case 'F':
            return new FCFSPager();
        case 'L':
            return new FCFSPager();
        case 'S':
            return new FCFSPager();
        default:
            return new FCFSPager();
    }
}

/**
 * Set the required output options on the arguments
 * @param - args - string that needs to parsed to fetch all the options that need to be set
 *
 */
void set_options(char *args) {
    char *ch = args;

    while(*ch != '\0') {
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
                printf("Error in options - %s\n", args);
                exit(1);
        }
    }
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
                NUM_FRAMES = atoi(optarg);
                break;
            case 'a':
                PAGER = getPager(optarg);
                break;
            case 'o':
                set_options(optarg);
                break;
            default:
                print_usage(argv[0]);
                exit(1);
        }
    }

    if (argc == optind) {
        printf("Not a valid inputfile <(null)>\n");
        exit(1);
    }

    if (argc == optind + 1) {
        printf("Not a valid random file <(null)>\n");
        exit(1);
    }

    if (!PAGER) {
        PAGER = new FCFSPager();
    }
}

/**
 * Parse the numbers from the random-number file
 * @param - filename - random-number file
 */
void parse_randoms(const char *filename) {
    fstream rand_file;
    rand_file.open(filename, ios::in);

    if (!rand_file.is_open()) {
        printf("Not a valid inputfile <%s>\n", filename);
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

int main(int argc, char **argv) {
    read_arguments(argc, argv);
}
