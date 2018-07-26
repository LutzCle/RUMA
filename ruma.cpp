#include <iostream>
#include <cstdlib>     // exit
#include <sys/syscall.h>
#include <sys/mman.h>   // mmap
#include <fcntl.h> // open
#include <unistd.h>     // ftruncate
#include <cassert>
#include <vector>

// char const BACKING_FILE_NAME[] = "/mnt/mmfs/small/rewire_backing_file";
// char const BACKING_FILE_NAME[] = "/mnt/mmfs/huge/rewire_backing_file";

// #define PAGE_SIZE (1 << 12) // small page
// #define PAGE_SIZE (1 << 21) // huge page
#define PAGE_INT_OFFSET (PAGE_SIZE / sizeof(int))

inline uint64_t start_counter() {
    uint32_t high, low;
    asm volatile(
            "cpuid\n"
            "rdtsc\n"
            "mov %%edx, %0\n"
            "mov %%eax, %1\n"
            : "=g" (high), "=g" (low)
            :
            : "%rax", "%rbx", "%rcx", "%rdx"
            );
    return ((uint64_t) high << 32) | low;
}

inline uint64_t stop_counter() {
    uint32_t high, low;
    asm volatile(
            "rdtscp\n"
            "mov %%edx, %0\n"
            "mov %%eax, %1\n"
            "cpuid\n"
            : "=g" (high), "=g" (low)
            :
            : "%rax", "%rbx", "%rcx", "%rdx"
            );
    return ((uint64_t) high << 32) | low;
}

uint64_t measure_tsc_overhead() {
    constexpr size_t COUNT = 10000;

    uint64_t overhead = UINT64_MAX;
    uint64_t start, stop;

    for (size_t i = 0; i < COUNT; ++i) {
        start = start_counter();
        asm volatile("");
        stop = stop_counter();
        if (stop - start < overhead) {
            overhead = stop - start;
        }
    }

    return overhead;
}

class RewiredMem {
public:
    RewiredMem(char const *file_name) : file_name_(file_name), fd_(0), vmem_(NULL), backed_size_(0), vspace_size_(0) {}

    RewiredMem(char const *file_name, size_t size, size_t maximum_size = 0)
        :
        file_name_(file_name)
    {
        int ret;

        assert(check_alignment(size, PAGE_SIZE));
        assert(check_alignment(maximum_size, PAGE_SIZE));

        // if ((fd_ = syscall(__NR_memfd_create, BACKING_FILE_NAME, 0)) == -1) {
        //     exit(fd_);
        // }

        if ((fd_ = open(file_name_, O_CREAT | O_RDWR), 0775) < 0) {
            perror("Open failed");
            exit(EXIT_FAILURE);
        }

        if (maximum_size) {
            vmem_ = mmap(NULL, maximum_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0);
            vspace_size_ = maximum_size;
        }
        else {
            vmem_ = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0);
            vspace_size_ = size;
        }
        if (vmem_ == MAP_FAILED) {
            perror("Map failed");
            unlink(file_name_);
            exit(EXIT_FAILURE);
        }

        if ((ret = ftruncate(fd_, size)) == -1) {
            perror("Resize with ftruncate failed");
            unlink(file_name_);
            exit(EXIT_FAILURE);
        }

        backed_size_ = size;
    }

    ~RewiredMem() {
        if (vmem_ != NULL && vspace_size_ != 0) {
            munmap(vmem_, vspace_size_);
        }

        if (fd_ != 0) {

            close(fd_);
            unlink(file_name_);
        }
    }

    uint64_t alloc_sequential(size_t size) {
        fd_ = syscall(__NR_memfd_create, BACKING_FILE_NAME, 0);
        vmem_ = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0);
        ftruncate(fd_, size);

        vspace_size_ = size;
        backed_size_ = size;
    }

    uint64_t alloc_random(size_t size) {
        size_t num_pages = size / PAGE_SIZE;
        std::vector<size_t> page_order(num_pages);

        for (size_t i = 0; i < page_order.size(); ++i) {
            page_order[i] = i;
        }

        fd_ = syscall(__NR_memfd_create, BACKING_FILE_NAME, 0);
        vmem_ = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0);

        for (size_t i = 0; i < page_order.size(); ++i) {
            mmap((char*)vmem_ + i * PAGE_SIZE, PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, fd_, page_order[i]);
        }

        ftruncate(fd_, size);

        vspace_size_ = size;
        backed_size_ = size;
    }

    void* begin() {
        return vmem_;
    }

    void* end() {
        return (char*)vmem_ + backed_size_;
    }

    size_t size() {
        return backed_size_;
    }

    void rewire(void *va, size_t pa, size_t size) {
        // Check page alignment
        assert(check_alignment(va, PAGE_SIZE));
        assert(check_alignment(pa, PAGE_SIZE));
        assert(check_alignment(size, PAGE_SIZE));

        mmap(va, size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, fd_, pa);
    }

    void append(size_t size) {
        assert(check_alignment(size, PAGE_SIZE));

        int ret;

        if ((ret = ftruncate(fd_, backed_size_ + size)) == -1) {
            exit(ret);
        }

        if (backed_size_ + size > vspace_size_) {
            if (mmap((char*)vmem_ + backed_size_, size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, fd_, backed_size_) == MAP_FAILED) {
                exit(EXIT_FAILURE);
            }
            vspace_size_ = backed_size_ + size;
        }

        backed_size_ += size;
    }

    void prepend(size_t size) {
        assert(check_alignment(size, PAGE_SIZE));

        ftruncate(fd_, backed_size_ + size);
        mmap((char*)vmem_ - size, size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, fd_, backed_size_);
    }

private:
    char const *file_name_;
    int fd_;
    void *vmem_;
    size_t backed_size_;
    size_t vspace_size_;

    static bool check_alignment(void *addr, size_t alignment) {
        return ((size_t)addr & (alignment - 1)) == 0;
    }

    static bool check_alignment(size_t size, size_t alignment) {
        return (size & (alignment - 1)) == 0;
    }

};

// Test remapping to different location
// Map frame 1 to page 0
void test_remap() {
    RewiredMem rmem(BACKING_FILE_NAME, PAGE_SIZE * 2);
    int *int_array = (int*) rmem.begin();

    int_array[PAGE_INT_OFFSET] = 1;
    std::cout << "Test remap | initial:  " << int_array[0] << " " << int_array[PAGE_INT_OFFSET] << std::endl;

    rmem.rewire(int_array, PAGE_SIZE, PAGE_SIZE);

    std::cout << "Test remap | remapped: " << int_array[0] << " " << int_array[PAGE_INT_OFFSET] << std::endl;
}

// Test appending pages to RewiredMem
void test_append() {
    RewiredMem rmem(BACKING_FILE_NAME, PAGE_SIZE * 10, PAGE_SIZE * 20);
    int *int_array = (int*) rmem.begin();

    size_t old_size = rmem.size() / sizeof(int);
    rmem.append(PAGE_SIZE * 1);

    int_array[old_size + PAGE_INT_OFFSET - 1] = 1;

    std::cout << "Test append: " << int_array[old_size + PAGE_INT_OFFSET - 1] << std::endl;
}

void measure_seq_write() {
    size_t array_bytes = 1024 * 1024 * 1024;
    size_t array_ints = array_bytes / sizeof(int);
    RewiredMem rmem(BACKING_FILE_NAME, array_bytes);
    int *int_array = (int*) rmem.begin();

    uint64_t start_1, stop_1, start_2, stop_2, start_3, stop_3;

    // First pass
    // Does allocation and zeroing of physical frames, and maps frames to pages
    start_1 = start_counter();
    for (size_t i = 0; i < array_ints; i += PAGE_INT_OFFSET) {
        int_array[i] = 1;
    }
    stop_1 = stop_counter();

    // Second pass
    // Frames should now already be allocated and mapped
    start_2 = start_counter();
    for (size_t i = 0; i < array_ints; i += PAGE_INT_OFFSET) {
        int_array[i] = 2;
    }
    stop_2 = stop_counter();

    // Third pass
    // Should be same as second pass
    start_3 = start_counter();
    for (size_t i = 0; i < array_ints; i += PAGE_INT_OFFSET) {
        int_array[i] = 2;
    }
    stop_3 = stop_counter();

    // Print raw times of write passes
    std::cout << "First pass  (cycles): " << stop_1 - start_1 << std::endl;
    std::cout << "Second pass (cycles): " << stop_2 - start_2 << std::endl;
    std::cout << "Third pass  (cycles): " << stop_3 - start_3 << std::endl;

    // Calculate average time of hard page fault per page
    size_t array_pages = array_bytes / PAGE_SIZE;
    std::cout << "Hard page fault (cycles / page): " << (stop_1 - start_1) / array_pages << std::endl;

    // Calculate average time of hard page fault per kilobyte
    std::cout << "Hard page fault (cycles / KiB):  " << (stop_1 - start_1) / (array_bytes / 1024) << std::endl;
}

int main() {

    measure_seq_write();
    test_remap();
    test_append();

    return EXIT_SUCCESS;
}
