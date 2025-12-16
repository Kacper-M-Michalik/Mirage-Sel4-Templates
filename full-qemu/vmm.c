#include <stdint.h>
#include <stdbool.h>
#include <stdalign.h>
#include <microkit.h>
#include <sddf/util/util.h>
#include <sddf/util/printf.h>
#include <sddf/serial/queue.h>
#include <sddf/serial/config.h>
#include <sddf/timer/client.h>
#include <sddf/timer/config.h>
#include <sddf/blk/queue.h>
#include <sddf/blk/storage_info.h>
#include <sddf/blk/config.h>
#include <solo5libvmm/guest.h>
#include <solo5libvmm/fault.h>
#include <solo5libvmm/solo5/hvt_abi.h>
#include <solo5libvmm/solo5/mft_abi.h>
#include <solo5libvmm/util.h>

// SDDF provides driver configs through named sections
__attribute__((__section__(".timer_client_config"))) timer_client_config_t timer_config;
__attribute__((__section__(".serial_client_config"))) serial_client_config_t serial_config;
__attribute__((__section__(".blk_client_config"))) blk_client_config_t blk_config;

// Handles to serial read and write queues respectively
serial_queue_handle_t rx_queue_handle;
serial_queue_handle_t tx_queue_handle;

// Handle to block queue
blk_queue_handle_t blk_queue;

// Data for the guest's kernel image
extern uint8_t _binary_guest_start[];
extern uint8_t _binary_guest_end[];
extern size_t _binary_guest_size[]; // Doesn't work without []

// Guest machine information
uint32_t guest_vcpu_id = 0;
uint8_t* guest_ram_vaddr = (uint8_t*)0x30000000;
size_t guest_ram_size= 0x10000000;

enum status 
{
    NONE,
    TIMEOUT,
    BLOCK_READ,
    BLOCK_WRITE
};
enum status vmm_status = NONE;

// Provide printf() for solo5libvmm
int printf(const char *fmt, ...) 
{
    va_list args;
    va_start(args, fmt);
    int ret = sddf_vprintf(fmt, args);
    va_end(args);
    return ret;
}

void init(void)
{    
    // Initialise serial read and write queues
    assert(serial_config_check_magic(&serial_config));
    serial_queue_init(&rx_queue_handle, serial_config.rx.queue.vaddr, serial_config.rx.data.size, serial_config.rx.data.vaddr);
    serial_queue_init(&tx_queue_handle, serial_config.tx.queue.vaddr, serial_config.tx.data.size, serial_config.tx.data.vaddr);
    // Initialise serial write function
    serial_putchar_init(serial_config.tx.id, &tx_queue_handle);

    // Initialise timer
    assert(timer_config_check_magic(&timer_config));

    // Initialise block
    assert(blk_config_check_magic(&blk_config));
    blk_queue_init(&blk_queue, blk_config.virt.req_queue.vaddr, blk_config.virt.resp_queue.vaddr, blk_config.virt.num_buffers);
    blk_storage_info_t* storage_info = blk_config.virt.storage_info.vaddr;
    while (!blk_storage_is_ready(storage_info));
    printf("Block device ready (sector_size=%ld, sectors=%ld, size=%ld bytes)\n", BLK_TRANSFER_SIZE, storage_info->capacity, storage_info->capacity * BLK_TRANSFER_SIZE);

    // Boot up guest
    printf("Guest memory addr: %zu\n", guest_ram_vaddr);
    printf("Guest image addr: %ld\n", _binary_guest_start);
    printf("Guest image size linked: %ld\n", _binary_guest_size);
    printf("Starting bootup\n");

    char cmdline[] = "";
    
    alignas(MFT1_NOTE_ALIGN) uint8_t mft_buff[MFT1_NOTE_MAX_SIZE];
    size_t mft_size;
    load_mft(_binary_guest_start, (size_t)_binary_guest_size, mft_buff, &mft_size);

    struct mft* mft = (struct mft*)mft_buff;
    mft->e[1].attached = true;
    mft->e[1].u.block_basic.block_size = BLK_TRANSFER_SIZE;
    mft->e[1].u.block_basic.capacity = (storage_info->capacity-1) * BLK_TRANSFER_SIZE;

    bool success = guest_setup_with_mft(guest_vcpu_id, _binary_guest_start, (size_t)_binary_guest_size, guest_ram_vaddr, guest_ram_size, 0, cmdline, strlen(cmdline), mft_buff, mft_size);
    printf("Load success: %d\n", success);

    if (success) guest_resume(guest_vcpu_id);
    else printf("Failed to load guest image\n");
}

void notified(microkit_channel ch)
{
    if (ch == serial_config.tx.id) return; // Write interrupt, we get this one time when we initialise the serial, but never see it again
    if (ch == serial_config.rx.id) return; // We got input from serial, ignore as solo5 doesn't support interrupt based input

    if (ch == timer_config.driver_id) // We got a notification about an elapsed timer, which we use to implement the poll hypercall
    {
        assert(vmm_status == TIMEOUT);
        vmm_status = NONE;

        guest_resume(guest_vcpu_id);        
        return;
    }
 
    if (ch == blk_config.virt.id) // We got response from the block device driver
    {
        assert(vmm_status == BLOCK_READ || vmm_status == BLOCK_WRITE);
        
        blk_resp_status_t status = -1;
        uint16_t count = -1;
        uint32_t id = -1;
        int err = blk_dequeue_resp(&blk_queue, &status, &count, &id);
        assert(!err);
        assert(status == BLK_RESP_OK);
        assert(count == 1);
        assert(id == 0);
        printf("Got block response!\n");

        if (vmm_status == BLOCK_READ)
        {

            // Copy data into guest
            //uint8_t* read_data = (uint8_t*)(blk_config.data.vaddr + (REQUEST_NUM_BLOCKS * BLK_TRANSFER_SIZE));
            //for (int i = 0; i < basic_data_len; i++) 
            //{
            //}
        }

        vmm_status = NONE;
        guest_resume(guest_vcpu_id);        
        return;
    }

    printf("Unexpected channel, ch: 0x%lx, vmm status: %ld\n", ch, vmm_status);  
}

seL4_Bool fault(microkit_child child, microkit_msginfo msginfo, microkit_msginfo *reply_msginfo) 
{    
    enum hvt_hypercall hc; 
    void* hc_data;

    // Handle the fault, if an unexpected fault has occured (i.e. anything other than a hypercall), the function will print appropriate error plus return false - which means the vcpu is not resumable
    // Your hypercall handler needs to seperately call guest_resume()
    bool was_handled = fault_handle(child, msginfo, guest_ram_vaddr, &hc, &hc_data, NULL);

    switch (hc)
    {
        case HVT_HYPERCALL_HALT:
            struct hvt_hc_halt* halt = (struct hvt_hc_halt*)hc_data;            
            printf("Guest exited with code: %ld\n", halt->exit_status);
            break;    
                   
        case HVT_HYPERCALL_WALLTIME:
            struct hvt_hc_walltime* walltime = (struct hvt_hc_walltime*)hc_data;
            uint64_t time = sddf_timer_time_now(timer_config.driver_id);
            walltime->nsecs = time;
            guest_resume(guest_vcpu_id);
            break;  

        case HVT_HYPERCALL_POLL:
            struct hvt_hc_poll* poll = (struct hvt_hc_poll*)hc_data;
            poll->ready_set = 0;
            poll->ret = 0;
            vmm_status = TIMEOUT;
            sddf_timer_set_timeout(timer_config.driver_id, poll->timeout_nsecs);  
            break;   

        case HVT_HYPERCALL_PUTS:
            struct hvt_hc_puts* puts = (struct hvt_hc_puts*)hc_data;     
            for (uint64_t i = 0; i < puts->len; i++)
            {
                uint64_t data = *(guest_ram_vaddr + puts->data + i);
                printf("%c", (char)data);
            }
            guest_resume(guest_vcpu_id);
            break;  

        case HVT_HYPERCALL_BLOCK_READ:
            struct hvt_hc_block_read* b_read = (struct hvt_hc_block_read*)hc_data;
            printf("Block Read hc (handle=%ld, size=%ld, offset=%ld, buff=%ld)\n", b_read->handle, b_read->len, b_read->offset, b_read->data);
            
            vmm_status == BLOCK_READ;
            assert(b_read->len == BLK_TRANSFER_SIZE);
            assert(b_read->offset % BLK_TRANSFER_SIZE == 0);

            int err = blk_enqueue_req(&blk_queue, BLK_REQ_READ, 0, b_read->offset / BLK_TRANSFER_SIZE, 1, 0);
            assert(!err);
            microkit_notify(blk_config.virt.id);
            break;

        case HVT_HYPERCALL_BLOCK_WRITE:
            struct hvt_hc_block_write* b_write = (struct hvt_hc_block_write*)hc_data;
            printf("Block Write hc (handle=%ld, size=%ld, offset=%ld, buff=%ld)\n", b_write->handle, b_write->len, b_write->offset, b_write->data);
            
            vmm_status = BLOCK_WRITE;
            assert(b_write->len <= BLK_TRANSFER_SIZE);
            assert(b_write->offset % BLK_TRANSFER_SIZE == 0);
            
            uint8_t* data_dest = (uint8_t*)blk_config.data.vaddr;
            memcpy(data_dest, (guest_ram_vaddr + b_write->data), b_write->len);
            err = blk_enqueue_req(&blk_queue, BLK_REQ_WRITE, 0, b_write->offset / BLK_TRANSFER_SIZE, 1, 0);
            assert(!err);
            microkit_notify(blk_config.virt.id);
            break;
            
        case HVT_HYPERCALL_NET_READ:
        case HVT_HYPERCALL_NET_WRITE:
        default:
            printf("Reached unimplemented hypercall");
            was_handled = false;
            break;
    }

    // Reply that the fault was handled successfully, this is not the same as resuming the VCPU in our case, fault_handle stops the VCPU in the case of a valid hypercall, your hypercall handlers need to manually call guest_resume() at the end
    if (was_handled)
    {
        *reply_msginfo = microkit_msginfo_new(0, 0);
        return seL4_True;
    }

    return seL4_False;    
}