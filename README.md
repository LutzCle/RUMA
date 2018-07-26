# Purpose

This repository is an independent reimplementation of the experiments presented in Schuhknecht et al. "RUMA Has It: Rewired User-space Memory Access is Possible!" VLDB 2016.

# Instructions

Setup huge pages using [Redhat's tuning guide](https://access.redhat.com/documentation/en-us/red_hat_enterprise_linux/5/html/tuning_and_optimizing_red_hat_enterprise_linux_for_oracle_9i_and_10g_databases/sect-oracle_9i_and_10g_tuning_guide-large_memory_optimization_big_pages_and_huge_pages-configuring_huge_pages_in_red_hat_enterprise_linux_4_or_5).

Create in-memory filesystems as backing storage, for small and huge pages:

    sudo mkdir -p /mnt/mmfs/small
    sudo mkdir -p /mnt/mmfs/huge

    sudo mount -t tmpfs -o gid=100,mode=0770 nodev /mnt/mmfs/small
    sudo mount -t hugetlbfs -o gid=100,mode=0770 nodev /mnt/mmfs/huge

Compile and run with script:

    chmod +x ruma.sh
    ./ruma.sh
