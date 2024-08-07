"""
Config parameters used by the snakemake workflow.
These can all appear in a workflow config/config files
"""

from enum import Enum

SEQS_FILE_LIST_PATH = 'seqs_file_list_path'
SEQS_DIR_PATH = "seqs_dir_path"

TMP_DIR = 'tmpdir'

PRIMARIZE_SAMPLES_SEPARATELY = 'primarize_samples_separately'

KMC_MAX_BINS="kmc_max_bins"
KMC_MEM_MB_PER_THREAD="kmc_mem_mb_per_thread"
KMC_MEM_OVERHEAD_FACTOR= "kmc_mem_overhead_factor"

SAMPLE_IDS_PATH="sample_ids_path"
SAMPLE_STAGING_SCRIPT_PATH="sample_staging_script_path"
SAMPLE_STAGING_SCRIPT_ADDITIONAL_OPTIONS="sample_staging_script_additional_options"
SAMPLE_STAGING_FILE_ENDING='sample_staging_file_ending'

BRWT_RELAX_ARITY="brwt_relax_arity"
BRWT_PARALLEL_NODES="brwt_parallel_nodes"
BRWT_LINKAGE_SUBSAMPLE="brwt_linkage_subsample"

MAX_THREADS = 'max_threads'
MAX_MEMORY_MB = 'max_memory_mb'
MAX_DISK_MB = 'max_disk_mb'
MAX_BUFFER_SIZE_MB = 'max_buffer_size_mb'

RULE_CONFIGS_KEY = 'rules'
THREADS_KEY = 'threads'
MEM_MB_KEY = 'mem_mb'
DISK_MB_KEY = 'disk_mb'

MEM_BUFFER_MB_KEY = 'mem_buffer_mb'
DISK_CAP_MB_KEY = 'disk_cap_mb'

GNU_TIME_CMD = 'gnu_time_cmd'


class AnnotationLabelsSource(Enum):
    SEQUENCE_HEADERS = 'sequence_headers'
    SEQUENCE_FILE_NAMES = 'sequence_file_names'

    def to_annotation_cmd_option(self):
        if self == self.SEQUENCE_FILE_NAMES:
            return '--anno-filename'
        elif self == self.SEQUENCE_HEADERS:
            return '--anno-header'
        else:
            raise ValueError(f"Invalid value of AnnotationLabelsSource: got {self}")


class AnnotationFormats(Enum):
    # COLUMN = 'column' # TODO: need special case in the workflow
    ROW = 'row'
    BIN_REL_WT = 'bin_rel_wt'
    FLAT = 'flat'
    RBFISH = 'rbfish'
    BRWT = 'brwt'
    RELAXED_BRWT = 'relax.brwt'
    RB_BRWT = 'rb_brwt'
    #RELAXED_RB_BRWT = 'relax.rb_brwt' # not possible
    ROW_DIFF_BRWT = 'row_diff_brwt'
    RELAXED_ROW_DIFF_BRWT = 'relax.row_diff_brwt'