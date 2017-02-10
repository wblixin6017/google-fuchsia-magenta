// Copyright 2017 The Fuchsia Authors. All rights reserved.
// This is a GENERATED file. The license governing this file can be found in the LICENSE file.

mx_time_t delegate_sys_time_get(
    uint32_t clock_id){
    return sys_time_get(clock_id);
}

mx_status_t delegate_sys_nanosleep(
    mx_time_t nanoseconds){
    return sys_nanosleep(nanoseconds);
}

mx_status_t delegate_sys_handle_close(
    mx_handle_t handle){
    return sys_handle_close(handle);
}

mx_status_t delegate_sys_handle_duplicate(
    mx_handle_t handle,
    mx_rights_t rights,
    mx_handle_t out[1]){
    return sys_handle_duplicate(handle, rights, out);
}

mx_status_t delegate_sys_handle_replace(
    mx_handle_t handle,
    mx_rights_t rights,
    mx_handle_t out[1]){
    return sys_handle_replace(handle, rights, out);
}

mx_status_t delegate_sys_handle_wait_one(
    mx_handle_t handle,
    mx_signals_t waitfor,
    mx_time_t timeout,
    mx_signals_t observed[1]){
    return sys_handle_wait_one(handle, waitfor, timeout, observed);
}

mx_status_t delegate_sys_handle_wait_many(
    mx_wait_item_t items[],
    uint32_t count,
    mx_time_t timeout){
    return sys_handle_wait_many(items, count, timeout);
}

mx_status_t delegate_sys_object_signal(
    mx_handle_t handle,
    uint32_t clear_mask,
    uint32_t set_mask){
    return sys_object_signal(handle, clear_mask, set_mask);
}

mx_status_t delegate_sys_object_signal_peer(
    mx_handle_t handle,
    uint32_t clear_mask,
    uint32_t set_mask){
    return sys_object_signal_peer(handle, clear_mask, set_mask);
}

mx_status_t delegate_sys_object_get_property(
    mx_handle_t handle,
    uint32_t property,
    void* value,
    size_t size){
    return sys_object_get_property(handle, property, value, size);
}

mx_status_t delegate_sys_object_set_property(
    mx_handle_t handle,
    uint32_t property,
    const void* value,
    size_t size){
    return sys_object_set_property(handle, property, value, size);
}

mx_status_t delegate_sys_object_get_info(
    mx_handle_t handle,
    uint32_t topic,
    void* buffer,
    size_t buffer_size,
    size_t actual_count[1],
    size_t avail_count[1]){
    return sys_object_get_info(handle, topic, buffer, buffer_size, actual_count, avail_count);
}

mx_status_t delegate_sys_object_get_child(
    mx_handle_t handle,
    uint64_t koid,
    mx_rights_t rights,
    mx_handle_t out[1]){
    return sys_object_get_child(handle, koid, rights, out);
}

mx_status_t delegate_sys_object_bind_exception_port(
    mx_handle_t object,
    mx_handle_t eport,
    uint64_t key,
    uint32_t options){
    return sys_object_bind_exception_port(object, eport, key, options);
}

mx_status_t delegate_sys_channel_create(
    uint32_t options,
    mx_handle_t out0[1],
    mx_handle_t out1[1]){
    return sys_channel_create(options, out0, out1);
}

mx_status_t delegate_sys_channel_read(
    mx_handle_t handle,
    uint32_t options,
    void* bytes,
    uint32_t num_bytes,
    uint32_t actual_bytes[1],
    mx_handle_t handles[],
    uint32_t num_handles,
    uint32_t actual_handles[1]){
    return sys_channel_read(handle, options, bytes, num_bytes, actual_bytes, handles, num_handles, actual_handles);
}

mx_status_t delegate_sys_channel_write(
    mx_handle_t handle,
    uint32_t options,
    const void* bytes,
    uint32_t num_bytes,
    const mx_handle_t handles[],
    uint32_t num_handles){
    return sys_channel_write(handle, options, bytes, num_bytes, handles, num_handles);
}

mx_status_t delegate_sys_channel_call(
    mx_handle_t handle,
    uint32_t options,
    mx_time_t timeout,
    const mx_channel_call_args_t args[1],
    uint32_t actual_bytes[1],
    uint32_t actual_handles[1],
    mx_status_t read_status[1]){
    return sys_channel_call(handle, options, timeout, args, actual_bytes, actual_handles, read_status);
}

mx_status_t delegate_sys_socket_create(
    uint32_t options,
    mx_handle_t out0[1],
    mx_handle_t out1[1]){
    return sys_socket_create(options, out0, out1);
}

mx_status_t delegate_sys_socket_write(
    mx_handle_t handle,
    uint32_t options,
    const void* buffer,
    size_t size,
    size_t actual[1]){
    return sys_socket_write(handle, options, buffer, size, actual);
}

mx_status_t delegate_sys_socket_read(
    mx_handle_t handle,
    uint32_t options,
    void* buffer,
    size_t size,
    size_t actual[1]){
    return sys_socket_read(handle, options, buffer, size, actual);
}

void delegate_sys_thread_exit(){
    sys_thread_exit();
}

mx_status_t delegate_sys_thread_create(
    mx_handle_t process,
    const char name[],
    uint32_t name_len,
    uint32_t options,
    mx_handle_t out[1]){
    return sys_thread_create(process, name, name_len, options, out);
}

mx_status_t delegate_sys_thread_start(
    mx_handle_t handle,
    uintptr_t thread_entry,
    uintptr_t stack,
    uintptr_t arg1,
    uintptr_t arg2){
    return sys_thread_start(handle, thread_entry, stack, arg1, arg2);
}

mx_status_t delegate_sys_thread_read_state(
    mx_handle_t handle,
    uint32_t kind,
    void* buffer,
    uint32_t len,
    uint32_t actual[1]){
    return sys_thread_read_state(handle, kind, buffer, len, actual);
}

mx_status_t delegate_sys_thread_write_state(
    mx_handle_t handle,
    uint32_t kind,
    const void* buffer,
    uint32_t buffer_len){
    return sys_thread_write_state(handle, kind, buffer, buffer_len);
}

void delegate_sys_process_exit(
    int retcode){
    sys_process_exit(retcode);
}

mx_status_t delegate_sys_process_create(
    mx_handle_t job,
    const char name[],
    uint32_t name_len,
    uint32_t options,
    mx_handle_t proc_handle[1],
    mx_handle_t vmar_handle[1]){
    return sys_process_create(job, name, name_len, options, proc_handle, vmar_handle);
}

mx_status_t delegate_sys_process_start(
    mx_handle_t process_handle,
    mx_handle_t thread_handle,
    uintptr_t entry,
    uintptr_t stack,
    mx_handle_t arg_handle,
    uintptr_t arg2){
    return sys_process_start(process_handle, thread_handle, entry, stack, arg_handle, arg2);
}

mx_status_t delegate_sys_process_read_memory(
    mx_handle_t proc,
    uintptr_t vaddr,
    void* buffer,
    size_t len,
    size_t actual[1]){
    return sys_process_read_memory(proc, vaddr, buffer, len, actual);
}

mx_status_t delegate_sys_process_write_memory(
    mx_handle_t proc,
    uintptr_t vaddr,
    const void* buffer,
    size_t len,
    size_t actual[1]){
    return sys_process_write_memory(proc, vaddr, buffer, len, actual);
}

mx_status_t delegate_sys_job_create(
    mx_handle_t parent_job,
    uint32_t options,
    mx_handle_t out[1]){
    return sys_job_create(parent_job, options, out);
}

mx_status_t delegate_sys_task_resume(
    mx_handle_t task_handle,
    uint32_t options){
    return sys_task_resume(task_handle, options);
}

mx_status_t delegate_sys_task_kill(
    mx_handle_t task_handle){
    return sys_task_kill(task_handle);
}

mx_status_t delegate_sys_event_create(
    uint32_t options,
    mx_handle_t out[1]){
    return sys_event_create(options, out);
}

mx_status_t delegate_sys_eventpair_create(
    uint32_t options,
    mx_handle_t out0[1],
    mx_handle_t out1[1]){
    return sys_eventpair_create(options, out0, out1);
}

mx_status_t delegate_sys_futex_wait(
    mx_futex_t value_ptr[1],
    int current_value,
    mx_time_t timeout){
    return sys_futex_wait(value_ptr, current_value, timeout);
}

mx_status_t delegate_sys_futex_wake(
    mx_futex_t value_ptr[1],
    uint32_t count){
    return sys_futex_wake(value_ptr, count);
}

mx_status_t delegate_sys_futex_requeue(
    mx_futex_t wake_ptr[1],
    uint32_t wake_count,
    int current_value,
    mx_futex_t requeue_ptr[1],
    uint32_t requeue_count){
    return sys_futex_requeue(wake_ptr, wake_count, current_value, requeue_ptr, requeue_count);
}

mx_status_t delegate_sys_waitset_create(
    uint32_t options,
    mx_handle_t out[1]){
    return sys_waitset_create(options, out);
}

mx_status_t delegate_sys_waitset_add(
    mx_handle_t waitset_handle,
    uint64_t cookie,
    mx_handle_t handle,
    mx_signals_t signals){
    return sys_waitset_add(waitset_handle, cookie, handle, signals);
}

mx_status_t delegate_sys_waitset_remove(
    mx_handle_t waitset_handle,
    uint64_t cookie){
    return sys_waitset_remove(waitset_handle, cookie);
}

mx_status_t delegate_sys_waitset_wait(
    mx_handle_t waitset_handle,
    mx_time_t timeout,
    mx_waitset_result_t results[],
    uint32_t count[1]){
    return sys_waitset_wait(waitset_handle, timeout, results, count);
}

mx_status_t delegate_sys_port_create(
    uint32_t options,
    mx_handle_t out[1]){
    return sys_port_create(options, out);
}

mx_status_t delegate_sys_port_queue(
    mx_handle_t handle,
    const void* packet,
    size_t size){
    return sys_port_queue(handle, packet, size);
}

mx_status_t delegate_sys_port_wait(
    mx_handle_t handle,
    mx_time_t timeout,
    void* packet,
    size_t size){
    return sys_port_wait(handle, timeout, packet, size);
}

mx_status_t delegate_sys_port_bind(
    mx_handle_t handle,
    uint64_t key,
    mx_handle_t source,
    mx_signals_t signals){
    return sys_port_bind(handle, key, source, signals);
}

mx_status_t delegate_sys_vmo_create(
    uint64_t size,
    uint32_t options,
    mx_handle_t out[1]){
    return sys_vmo_create(size, options, out);
}

mx_status_t delegate_sys_vmo_read(
    mx_handle_t handle,
    void* data,
    uint64_t offset,
    size_t len,
    size_t actual[1]){
    return sys_vmo_read(handle, data, offset, len, actual);
}

mx_status_t delegate_sys_vmo_write(
    mx_handle_t handle,
    const void* data,
    uint64_t offset,
    size_t len,
    size_t actual[1]){
    return sys_vmo_write(handle, data, offset, len, actual);
}

mx_status_t delegate_sys_vmo_get_size(
    mx_handle_t handle,
    uint64_t size[1]){
    return sys_vmo_get_size(handle, size);
}

mx_status_t delegate_sys_vmo_set_size(
    mx_handle_t handle,
    uint64_t size){
    return sys_vmo_set_size(handle, size);
}

mx_status_t delegate_sys_vmo_op_range(
    mx_handle_t handle,
    uint32_t op,
    uint64_t offset,
    uint64_t size,
    void* buffer,
    size_t buffer_size){
    return sys_vmo_op_range(handle, op, offset, size, buffer, buffer_size);
}

mx_status_t delegate_sys_cprng_draw(
    void* buffer,
    size_t len,
    size_t actual[1]){
    return sys_cprng_draw(buffer, len, actual);
}

mx_status_t delegate_sys_cprng_add_entropy(
    const void* buffer,
    size_t len){
    return sys_cprng_add_entropy(buffer, len);
}

mx_status_t delegate_sys_fifo0_create(
    uint64_t count,
    mx_handle_t out[1]){
    return sys_fifo0_create(count, out);
}

mx_status_t delegate_sys_fifo0_op(
    mx_handle_t handle,
    uint32_t op,
    uint64_t val,
    mx_fifo_state_t out[1]){
    return sys_fifo0_op(handle, op, val, out);
}

mx_status_t delegate_sys_log_create(
    uint32_t options,
    mx_handle_t out[1]){
    return sys_log_create(options, out);
}

mx_status_t delegate_sys_log_write(
    mx_handle_t handle,
    uint32_t len,
    const void* buffer,
    uint32_t options){
    return sys_log_write(handle, len, buffer, options);
}

mx_status_t delegate_sys_log_read(
    mx_handle_t handle,
    uint32_t len,
    void* buffer,
    uint32_t options){
    return sys_log_read(handle, len, buffer, options);
}

mx_status_t delegate_sys_ktrace_read(
    mx_handle_t handle,
    void* data,
    uint32_t offset,
    uint32_t len,
    uint32_t actual[1]){
    return sys_ktrace_read(handle, data, offset, len, actual);
}

mx_status_t delegate_sys_ktrace_control(
    mx_handle_t handle,
    uint32_t action,
    uint32_t options,
    void* ptr){
    return sys_ktrace_control(handle, action, options, ptr);
}

mx_status_t delegate_sys_ktrace_write(
    mx_handle_t handle,
    uint32_t id,
    uint32_t arg0,
    uint32_t arg1){
    return sys_ktrace_write(handle, id, arg0, arg1);
}

mx_status_t delegate_sys_mtrace_control(
    mx_handle_t handle,
    uint32_t kind,
    uint32_t action,
    uint32_t options,
    void* ptr,
    uint32_t size){
    return sys_mtrace_control(handle, kind, action, options, ptr, size);
}

mx_handle_t delegate_sys_debug_transfer_handle(
    mx_handle_t proc,
    mx_handle_t handle){
    return sys_debug_transfer_handle(proc, handle);
}

mx_status_t delegate_sys_debug_read(
    mx_handle_t handle,
    void* buffer,
    uint32_t length){
    return sys_debug_read(handle, buffer, length);
}

mx_status_t delegate_sys_debug_write(
    const void* buffer,
    uint32_t length){
    return sys_debug_write(buffer, length);
}

mx_status_t delegate_sys_debug_send_command(
    mx_handle_t resource_handle,
    const void* buffer,
    uint32_t length){
    return sys_debug_send_command(resource_handle, buffer, length);
}

mx_handle_t delegate_sys_interrupt_create(
    mx_handle_t handle,
    uint32_t vector,
    uint32_t options){
    return sys_interrupt_create(handle, vector, options);
}

mx_status_t delegate_sys_interrupt_complete(
    mx_handle_t handle){
    return sys_interrupt_complete(handle);
}

mx_status_t delegate_sys_interrupt_wait(
    mx_handle_t handle){
    return sys_interrupt_wait(handle);
}

mx_status_t delegate_sys_mmap_device_io(
    mx_handle_t handle,
    uint32_t io_addr,
    uint32_t len){
    return sys_mmap_device_io(handle, io_addr, len);
}

mx_status_t delegate_sys_mmap_device_memory(
    mx_handle_t handle,
    mx_paddr_t paddr,
    uint32_t len,
    mx_cache_policy_t cache_policy,
    uintptr_t out_vaddr[1]){
    return sys_mmap_device_memory(handle, paddr, len, cache_policy, out_vaddr);
}

mx_status_t delegate_sys_io_mapping_get_info(
    mx_handle_t handle,
    uintptr_t out_vaddr[1],
    uint64_t out_size[1]){
    return sys_io_mapping_get_info(handle, out_vaddr, out_size);
}

mx_status_t delegate_sys_vmo_create_contiguous(
    mx_handle_t rsrc_handle,
    size_t size,
    uint32_t alignment_log2,
    mx_handle_t out[1]){
    return sys_vmo_create_contiguous(rsrc_handle, size, alignment_log2, out);
}

mx_status_t delegate_sys_vmar_allocate(
    mx_handle_t parent_vmar_handle,
    size_t offset,
    size_t size,
    uint32_t flags,
    mx_handle_t child_vmar[1],
    uintptr_t child_addr[1]){
    return sys_vmar_allocate(parent_vmar_handle, offset, size, flags, child_vmar, child_addr);
}

mx_status_t delegate_sys_vmar_destroy(
    mx_handle_t vmar_handle){
    return sys_vmar_destroy(vmar_handle);
}

mx_status_t delegate_sys_vmar_map(
    mx_handle_t vmar_handle,
    size_t vmar_offset,
    mx_handle_t vmo_handle,
    uint64_t vmo_offset,
    size_t len,
    uint32_t flags,
    uintptr_t mapped_addr[1]){
    return sys_vmar_map(vmar_handle, vmar_offset, vmo_handle, vmo_offset, len, flags, mapped_addr);
}

mx_status_t delegate_sys_vmar_unmap(
    mx_handle_t vmar_handle,
    uintptr_t addr,
    size_t len){
    return sys_vmar_unmap(vmar_handle, addr, len);
}

mx_status_t delegate_sys_vmar_protect(
    mx_handle_t vmar_handle,
    uintptr_t addr,
    size_t len,
    uint32_t prot){
    return sys_vmar_protect(vmar_handle, addr, len, prot);
}

mx_status_t delegate_sys_bootloader_fb_get_info(
    uint32_t format[1],
    uint32_t width[1],
    uint32_t height[1],
    uint32_t stride[1]){
    return sys_bootloader_fb_get_info(format, width, height, stride);
}

mx_status_t delegate_sys_set_framebuffer(
    mx_handle_t handle,
    void* vaddr,
    uint32_t len,
    uint32_t format,
    uint32_t width,
    uint32_t height,
    uint32_t stride){
    return sys_set_framebuffer(handle, vaddr, len, format, width, height, stride);
}

mx_status_t delegate_sys_clock_adjust(
    mx_handle_t handle,
    uint32_t clock_id,
    int64_t offset){
    return sys_clock_adjust(handle, clock_id, offset);
}

mx_handle_t delegate_sys_pci_get_nth_device(
    mx_handle_t handle,
    uint32_t index,
    mx_pcie_get_nth_info_t out_info[1]){
    return sys_pci_get_nth_device(handle, index, out_info);
}

mx_status_t delegate_sys_pci_claim_device(
    mx_handle_t handle){
    return sys_pci_claim_device(handle);
}

mx_status_t delegate_sys_pci_enable_bus_master(
    mx_handle_t handle,
    bool enable){
    return sys_pci_enable_bus_master(handle, enable);
}

mx_status_t delegate_sys_pci_enable_pio(
    mx_handle_t handle,
    bool enable){
    return sys_pci_enable_pio(handle, enable);
}

mx_status_t delegate_sys_pci_reset_device(
    mx_handle_t handle){
    return sys_pci_reset_device(handle);
}

mx_handle_t delegate_sys_pci_map_mmio(
    mx_handle_t handle,
    uint32_t bar_num,
    mx_cache_policy_t cache_policy){
    return sys_pci_map_mmio(handle, bar_num, cache_policy);
}

mx_status_t delegate_sys_pci_io_write(
    mx_handle_t handle,
    uint32_t bar_num,
    uint32_t offset,
    uint32_t len,
    uint32_t value){
    return sys_pci_io_write(handle, bar_num, offset, len, value);
}

mx_status_t delegate_sys_pci_io_read(
    mx_handle_t handle,
    uint32_t bar_num,
    uint32_t offset,
    uint32_t len,
    uint32_t out_value[1]){
    return sys_pci_io_read(handle, bar_num, offset, len, out_value);
}

mx_handle_t delegate_sys_pci_map_interrupt(
    mx_handle_t handle,
    int32_t which_irq){
    return sys_pci_map_interrupt(handle, which_irq);
}

mx_handle_t delegate_sys_pci_map_config(
    mx_handle_t handle){
    return sys_pci_map_config(handle);
}

mx_status_t delegate_sys_pci_query_irq_mode_caps(
    mx_handle_t handle,
    uint32_t mode,
    uint32_t out_max_irqs[1]){
    return sys_pci_query_irq_mode_caps(handle, mode, out_max_irqs);
}

mx_status_t delegate_sys_pci_set_irq_mode(
    mx_handle_t handle,
    uint32_t mode,
    uint32_t requested_irq_count){
    return sys_pci_set_irq_mode(handle, mode, requested_irq_count);
}

mx_status_t delegate_sys_pci_init(
    mx_handle_t handle,
    const mx_pci_init_arg_t init_buf[],
    uint32_t len){
    return sys_pci_init(handle, init_buf, len);
}

mx_status_t delegate_sys_pci_add_subtract_io_range(
    mx_handle_t handle,
    bool mmio,
    uint64_t base,
    uint64_t len,
    bool add){
    return sys_pci_add_subtract_io_range(handle, mmio, base, len, add);
}

uint32_t delegate_sys_acpi_uefi_rsdp(
    mx_handle_t handle){
    return sys_acpi_uefi_rsdp(handle);
}

mx_status_t delegate_sys_acpi_cache_flush(
    mx_handle_t handle){
    return sys_acpi_cache_flush(handle);
}

mx_status_t delegate_sys_resource_create(
    mx_handle_t parent_handle,
    const mx_rrec_t records[],
    uint32_t count,
    mx_handle_t resource_out[1]){
    return sys_resource_create(parent_handle, records, count, resource_out);
}

mx_status_t delegate_sys_resource_get_handle(
    mx_handle_t handle,
    uint32_t index,
    uint32_t options,
    mx_handle_t out[1]){
    return sys_resource_get_handle(handle, index, options, out);
}

mx_status_t delegate_sys_resource_do_action(
    mx_handle_t handle,
    uint32_t index,
    uint32_t action,
    uint32_t arg0,
    uint32_t arg1){
    return sys_resource_do_action(handle, index, action, arg0, arg1);
}

mx_status_t delegate_sys_resource_connect(
    mx_handle_t handle,
    mx_handle_t channel){
    return sys_resource_connect(handle, channel);
}

mx_status_t delegate_sys_resource_accept(
    mx_handle_t handle,
    mx_handle_t channel[1]){
    return sys_resource_accept(handle, channel);
}

int delegate_sys_syscall_test_0(){
    return sys_syscall_test_0();
}

int delegate_sys_syscall_test_1(
    int a){
    return sys_syscall_test_1(a);
}

int delegate_sys_syscall_test_2(
    int a,
    int b){
    return sys_syscall_test_2(a, b);
}

int delegate_sys_syscall_test_3(
    int a,
    int b,
    int c){
    return sys_syscall_test_3(a, b, c);
}

int delegate_sys_syscall_test_4(
    int a,
    int b,
    int c,
    int d){
    return sys_syscall_test_4(a, b, c, d);
}

int delegate_sys_syscall_test_5(
    int a,
    int b,
    int c,
    int d,
    int e){
    return sys_syscall_test_5(a, b, c, d, e);
}

int delegate_sys_syscall_test_6(
    int a,
    int b,
    int c,
    int d,
    int e,
    int f){
    return sys_syscall_test_6(a, b, c, d, e, f);
}

int delegate_sys_syscall_test_7(
    int a,
    int b,
    int c,
    int d,
    int e,
    int f,
    int g){
    return sys_syscall_test_7(a, b, c, d, e, f, g);
}

int delegate_sys_syscall_test_8(
    int a,
    int b,
    int c,
    int d,
    int e,
    int f,
    int g,
    int h){
    return sys_syscall_test_8(a, b, c, d, e, f, g, h);
}


