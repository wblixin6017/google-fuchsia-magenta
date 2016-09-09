typedef struct verity_device {
    mx_device_t dev;
    lba_t num_leaves;
    lba_t num_blocks;

    verity_mode_t mode;
    mtx_t mode_mtx;

    uint64_t* bitmap;
    size_t bitmap_len;
    mtx_t bitmap_mtx;

    list_node_t iotxns;
    mtx_t iotxns_mtx;

    list_node_t to_verify;
    mtx_t verifier_mtx;
    cnd_t verifier_cnd;

    list_node_t to_digest;
    mtx_t digester_mtx;
    cnd_t digester_cnd;

    thrd_t threads[VERITY_VERIFIER_THREADS + VERITY_DIGESTER_THREADS];
    size_t num_threads;

    list_node_t levels;
} verity_device_t;


