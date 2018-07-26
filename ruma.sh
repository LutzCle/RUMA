#!/bin/bash

# Tip: Follow guide at https://access.redhat.com/documentation/en-US/Red_Hat_Enterprise_Linux/5/html/Tuning_and_Optimizing_Red_Hat_Enterprise_Linux_for_Oracle_9i_and_10g_Databases/sect-Oracle_9i_and_10g_Tuning_Guide-Large_Memory_Optimization_Big_Pages_and_Huge_Pages-Configuring_Huge_Pages_in_Red_Hat_Enterprise_Linux_4_or_5.html

# Create filesystems
# sudo mkdir -p /mnt/mmfs/small
# sudo mkdir -p /mnt/mmfs/huge
#
# sudo mount -t tmpfs -o gid=100,mode=0770 nodev /mnt/mmfs/small
# sudo mount -t hugetlbfs -o gid=100,mode=0770 nodev /mnt/mmfs/huge

# Compile
g++ -g ruma.cpp -o ruma_small -DBACKING_FILE_NAME=\"/mnt/mmfs/small/rewire_backing_file\" -DPAGE_SIZE="(1 << 12)"
g++ -g ruma.cpp -o ruma_huge -DBACKING_FILE_NAME=\"/mnt/mmfs/huge/rewire_backing_file\" -DPAGE_SIZE="(1 << 21)"

# Run
echo "=== Small pages ==="

./ruma_small

echo "=== Huge pages ==="

./ruma_huge
