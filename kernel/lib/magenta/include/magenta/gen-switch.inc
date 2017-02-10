// Copyright 2017 The Fuchsia Authors. All rights reserved.
// This is a GENERATED file. The license governing this file can be found in the LICENSE file.

    case 0: ret = reinterpret_cast<syscall_func1>(sys_time_get)(
        arg1);
       break;
    case 1: ret = reinterpret_cast<syscall_func1>(sys_nanosleep)(
        arg1);
       break;
    case 7: ret = reinterpret_cast<syscall_func1>(sys_handle_close)(
        arg1);
       break;
    case 8: ret = reinterpret_cast<syscall_func3>(sys_handle_duplicate)(
        arg1,
        arg2,
        arg3);
       break;
    case 9: ret = reinterpret_cast<syscall_func3>(sys_handle_replace)(
        arg1,
        arg2,
        arg3);
       break;
    case 10: ret = reinterpret_cast<syscall_func4>(sys_handle_wait_one)(
        arg1,
        arg2,
        arg3,
        arg4);
       break;
    case 11: ret = reinterpret_cast<syscall_func3>(sys_handle_wait_many)(
        arg1,
        arg2,
        arg3);
       break;
    case 12: ret = reinterpret_cast<syscall_func3>(sys_object_signal)(
        arg1,
        arg2,
        arg3);
       break;
    case 13: ret = reinterpret_cast<syscall_func3>(sys_object_signal_peer)(
        arg1,
        arg2,
        arg3);
       break;
    case 14: ret = reinterpret_cast<syscall_func4>(sys_object_get_property)(
        arg1,
        arg2,
        arg3,
        arg4);
       break;
    case 15: ret = reinterpret_cast<syscall_func4>(sys_object_set_property)(
        arg1,
        arg2,
        arg3,
        arg4);
       break;
    case 16: ret = reinterpret_cast<syscall_func6>(sys_object_get_info)(
        arg1,
        arg2,
        arg3,
        arg4,
        arg5,
        arg6);
       break;
    case 17: ret = reinterpret_cast<syscall_func4>(sys_object_get_child)(
        arg1,
        arg2,
        arg3,
        arg4);
       break;
    case 18: ret = reinterpret_cast<syscall_func4>(sys_object_bind_exception_port)(
        arg1,
        arg2,
        arg3,
        arg4);
       break;
    case 19: ret = reinterpret_cast<syscall_func3>(sys_channel_create)(
        arg1,
        arg2,
        arg3);
       break;
    case 20: ret = reinterpret_cast<syscall_func8>(sys_channel_read)(
        arg1,
        arg2,
        arg3,
        arg4,
        arg5,
        arg6,
        arg7,
        arg8);
       break;
    case 21: ret = reinterpret_cast<syscall_func6>(sys_channel_write)(
        arg1,
        arg2,
        arg3,
        arg4,
        arg5,
        arg6);
       break;
    case 22: ret = reinterpret_cast<syscall_func7>(sys_channel_call)(
        arg1,
        arg2,
        arg3,
        arg4,
        arg5,
        arg6,
        arg7);
       break;
    case 23: ret = reinterpret_cast<syscall_func3>(sys_socket_create)(
        arg1,
        arg2,
        arg3);
       break;
    case 24: ret = reinterpret_cast<syscall_func5>(sys_socket_write)(
        arg1,
        arg2,
        arg3,
        arg4,
        arg5);
       break;
    case 25: ret = reinterpret_cast<syscall_func5>(sys_socket_read)(
        arg1,
        arg2,
        arg3,
        arg4,
        arg5);
       break;
    case 26: ret = 0; reinterpret_cast<syscall_func0>(sys_thread_exit)();
       break;
    case 27: ret = reinterpret_cast<syscall_func5>(sys_thread_create)(
        arg1,
        arg2,
        arg3,
        arg4,
        arg5);
       break;
    case 28: ret = reinterpret_cast<syscall_func5>(sys_thread_start)(
        arg1,
        arg2,
        arg3,
        arg4,
        arg5);
       break;
    case 29: ret = reinterpret_cast<syscall_func5>(sys_thread_read_state)(
        arg1,
        arg2,
        arg3,
        arg4,
        arg5);
       break;
    case 30: ret = reinterpret_cast<syscall_func4>(sys_thread_write_state)(
        arg1,
        arg2,
        arg3,
        arg4);
       break;
    case 31: ret = 0; reinterpret_cast<syscall_func1>(sys_process_exit)(
        arg1);
       break;
    case 32: ret = reinterpret_cast<syscall_func6>(sys_process_create)(
        arg1,
        arg2,
        arg3,
        arg4,
        arg5,
        arg6);
       break;
    case 33: ret = reinterpret_cast<syscall_func6>(sys_process_start)(
        arg1,
        arg2,
        arg3,
        arg4,
        arg5,
        arg6);
       break;
    case 34: ret = reinterpret_cast<syscall_func5>(sys_process_read_memory)(
        arg1,
        arg2,
        arg3,
        arg4,
        arg5);
       break;
    case 35: ret = reinterpret_cast<syscall_func5>(sys_process_write_memory)(
        arg1,
        arg2,
        arg3,
        arg4,
        arg5);
       break;
    case 36: ret = reinterpret_cast<syscall_func3>(sys_job_create)(
        arg1,
        arg2,
        arg3);
       break;
    case 37: ret = reinterpret_cast<syscall_func2>(sys_task_resume)(
        arg1,
        arg2);
       break;
    case 38: ret = reinterpret_cast<syscall_func1>(sys_task_kill)(
        arg1);
       break;
    case 39: ret = reinterpret_cast<syscall_func2>(sys_event_create)(
        arg1,
        arg2);
       break;
    case 40: ret = reinterpret_cast<syscall_func3>(sys_eventpair_create)(
        arg1,
        arg2,
        arg3);
       break;
    case 41: ret = reinterpret_cast<syscall_func3>(sys_futex_wait)(
        arg1,
        arg2,
        arg3);
       break;
    case 42: ret = reinterpret_cast<syscall_func2>(sys_futex_wake)(
        arg1,
        arg2);
       break;
    case 43: ret = reinterpret_cast<syscall_func5>(sys_futex_requeue)(
        arg1,
        arg2,
        arg3,
        arg4,
        arg5);
       break;
    case 44: ret = reinterpret_cast<syscall_func2>(sys_waitset_create)(
        arg1,
        arg2);
       break;
    case 45: ret = reinterpret_cast<syscall_func4>(sys_waitset_add)(
        arg1,
        arg2,
        arg3,
        arg4);
       break;
    case 46: ret = reinterpret_cast<syscall_func2>(sys_waitset_remove)(
        arg1,
        arg2);
       break;
    case 47: ret = reinterpret_cast<syscall_func4>(sys_waitset_wait)(
        arg1,
        arg2,
        arg3,
        arg4);
       break;
    case 48: ret = reinterpret_cast<syscall_func2>(sys_port_create)(
        arg1,
        arg2);
       break;
    case 49: ret = reinterpret_cast<syscall_func3>(sys_port_queue)(
        arg1,
        arg2,
        arg3);
       break;
    case 50: ret = reinterpret_cast<syscall_func4>(sys_port_wait)(
        arg1,
        arg2,
        arg3,
        arg4);
       break;
    case 51: ret = reinterpret_cast<syscall_func4>(sys_port_bind)(
        arg1,
        arg2,
        arg3,
        arg4);
       break;
    case 52: ret = reinterpret_cast<syscall_func3>(sys_vmo_create)(
        arg1,
        arg2,
        arg3);
       break;
    case 53: ret = reinterpret_cast<syscall_func5>(sys_vmo_read)(
        arg1,
        arg2,
        arg3,
        arg4,
        arg5);
       break;
    case 54: ret = reinterpret_cast<syscall_func5>(sys_vmo_write)(
        arg1,
        arg2,
        arg3,
        arg4,
        arg5);
       break;
    case 55: ret = reinterpret_cast<syscall_func2>(sys_vmo_get_size)(
        arg1,
        arg2);
       break;
    case 56: ret = reinterpret_cast<syscall_func2>(sys_vmo_set_size)(
        arg1,
        arg2);
       break;
    case 57: ret = reinterpret_cast<syscall_func6>(sys_vmo_op_range)(
        arg1,
        arg2,
        arg3,
        arg4,
        arg5,
        arg6);
       break;
    case 58: ret = reinterpret_cast<syscall_func3>(sys_cprng_draw)(
        arg1,
        arg2,
        arg3);
       break;
    case 59: ret = reinterpret_cast<syscall_func2>(sys_cprng_add_entropy)(
        arg1,
        arg2);
       break;
    case 60: ret = reinterpret_cast<syscall_func2>(sys_fifo0_create)(
        arg1,
        arg2);
       break;
    case 61: ret = reinterpret_cast<syscall_func4>(sys_fifo0_op)(
        arg1,
        arg2,
        arg3,
        arg4);
       break;
    case 62: ret = reinterpret_cast<syscall_func2>(sys_log_create)(
        arg1,
        arg2);
       break;
    case 63: ret = reinterpret_cast<syscall_func4>(sys_log_write)(
        arg1,
        arg2,
        arg3,
        arg4);
       break;
    case 64: ret = reinterpret_cast<syscall_func4>(sys_log_read)(
        arg1,
        arg2,
        arg3,
        arg4);
       break;
    case 65: ret = reinterpret_cast<syscall_func5>(sys_ktrace_read)(
        arg1,
        arg2,
        arg3,
        arg4,
        arg5);
       break;
    case 66: ret = reinterpret_cast<syscall_func4>(sys_ktrace_control)(
        arg1,
        arg2,
        arg3,
        arg4);
       break;
    case 67: ret = reinterpret_cast<syscall_func4>(sys_ktrace_write)(
        arg1,
        arg2,
        arg3,
        arg4);
       break;
    case 68: ret = reinterpret_cast<syscall_func6>(sys_mtrace_control)(
        arg1,
        arg2,
        arg3,
        arg4,
        arg5,
        arg6);
       break;
    case 69: ret = reinterpret_cast<syscall_func2>(sys_debug_transfer_handle)(
        arg1,
        arg2);
       break;
    case 70: ret = reinterpret_cast<syscall_func3>(sys_debug_read)(
        arg1,
        arg2,
        arg3);
       break;
    case 71: ret = reinterpret_cast<syscall_func2>(sys_debug_write)(
        arg1,
        arg2);
       break;
    case 72: ret = reinterpret_cast<syscall_func3>(sys_debug_send_command)(
        arg1,
        arg2,
        arg3);
       break;
    case 73: ret = reinterpret_cast<syscall_func3>(sys_interrupt_create)(
        arg1,
        arg2,
        arg3);
       break;
    case 74: ret = reinterpret_cast<syscall_func1>(sys_interrupt_complete)(
        arg1);
       break;
    case 75: ret = reinterpret_cast<syscall_func1>(sys_interrupt_wait)(
        arg1);
       break;
    case 76: ret = reinterpret_cast<syscall_func3>(sys_mmap_device_io)(
        arg1,
        arg2,
        arg3);
       break;
    case 77: ret = reinterpret_cast<syscall_func5>(sys_mmap_device_memory)(
        arg1,
        arg2,
        arg3,
        arg4,
        arg5);
       break;
    case 78: ret = reinterpret_cast<syscall_func3>(sys_io_mapping_get_info)(
        arg1,
        arg2,
        arg3);
       break;
    case 79: ret = reinterpret_cast<syscall_func4>(sys_vmo_create_contiguous)(
        arg1,
        arg2,
        arg3,
        arg4);
       break;
    case 80: ret = reinterpret_cast<syscall_func6>(sys_vmar_allocate)(
        arg1,
        arg2,
        arg3,
        arg4,
        arg5,
        arg6);
       break;
    case 81: ret = reinterpret_cast<syscall_func1>(sys_vmar_destroy)(
        arg1);
       break;
    case 82: ret = reinterpret_cast<syscall_func7>(sys_vmar_map)(
        arg1,
        arg2,
        arg3,
        arg4,
        arg5,
        arg6,
        arg7);
       break;
    case 83: ret = reinterpret_cast<syscall_func3>(sys_vmar_unmap)(
        arg1,
        arg2,
        arg3);
       break;
    case 84: ret = reinterpret_cast<syscall_func4>(sys_vmar_protect)(
        arg1,
        arg2,
        arg3,
        arg4);
       break;
    case 85: ret = reinterpret_cast<syscall_func4>(sys_bootloader_fb_get_info)(
        arg1,
        arg2,
        arg3,
        arg4);
       break;
    case 86: ret = reinterpret_cast<syscall_func7>(sys_set_framebuffer)(
        arg1,
        arg2,
        arg3,
        arg4,
        arg5,
        arg6,
        arg7);
       break;
    case 87: ret = reinterpret_cast<syscall_func3>(sys_clock_adjust)(
        arg1,
        arg2,
        arg3);
       break;
    case 88: ret = reinterpret_cast<syscall_func3>(sys_pci_get_nth_device)(
        arg1,
        arg2,
        arg3);
       break;
    case 89: ret = reinterpret_cast<syscall_func1>(sys_pci_claim_device)(
        arg1);
       break;
    case 90: ret = reinterpret_cast<syscall_func2>(sys_pci_enable_bus_master)(
        arg1,
        arg2);
       break;
    case 91: ret = reinterpret_cast<syscall_func2>(sys_pci_enable_pio)(
        arg1,
        arg2);
       break;
    case 92: ret = reinterpret_cast<syscall_func1>(sys_pci_reset_device)(
        arg1);
       break;
    case 93: ret = reinterpret_cast<syscall_func3>(sys_pci_map_mmio)(
        arg1,
        arg2,
        arg3);
       break;
    case 94: ret = reinterpret_cast<syscall_func5>(sys_pci_io_write)(
        arg1,
        arg2,
        arg3,
        arg4,
        arg5);
       break;
    case 95: ret = reinterpret_cast<syscall_func5>(sys_pci_io_read)(
        arg1,
        arg2,
        arg3,
        arg4,
        arg5);
       break;
    case 96: ret = reinterpret_cast<syscall_func2>(sys_pci_map_interrupt)(
        arg1,
        arg2);
       break;
    case 97: ret = reinterpret_cast<syscall_func1>(sys_pci_map_config)(
        arg1);
       break;
    case 98: ret = reinterpret_cast<syscall_func3>(sys_pci_query_irq_mode_caps)(
        arg1,
        arg2,
        arg3);
       break;
    case 99: ret = reinterpret_cast<syscall_func3>(sys_pci_set_irq_mode)(
        arg1,
        arg2,
        arg3);
       break;
    case 100: ret = reinterpret_cast<syscall_func3>(sys_pci_init)(
        arg1,
        arg2,
        arg3);
       break;
    case 101: ret = reinterpret_cast<syscall_func5>(sys_pci_add_subtract_io_range)(
        arg1,
        arg2,
        arg3,
        arg4,
        arg5);
       break;
    case 102: ret = reinterpret_cast<syscall_func1>(sys_acpi_uefi_rsdp)(
        arg1);
       break;
    case 103: ret = reinterpret_cast<syscall_func1>(sys_acpi_cache_flush)(
        arg1);
       break;
    case 104: ret = reinterpret_cast<syscall_func4>(sys_resource_create)(
        arg1,
        arg2,
        arg3,
        arg4);
       break;
    case 105: ret = reinterpret_cast<syscall_func4>(sys_resource_get_handle)(
        arg1,
        arg2,
        arg3,
        arg4);
       break;
    case 106: ret = reinterpret_cast<syscall_func5>(sys_resource_do_action)(
        arg1,
        arg2,
        arg3,
        arg4,
        arg5);
       break;
    case 107: ret = reinterpret_cast<syscall_func2>(sys_resource_connect)(
        arg1,
        arg2);
       break;
    case 108: ret = reinterpret_cast<syscall_func2>(sys_resource_accept)(
        arg1,
        arg2);
       break;
    case 109: ret = reinterpret_cast<syscall_func0>(sys_syscall_test_0)();
       break;
    case 110: ret = reinterpret_cast<syscall_func1>(sys_syscall_test_1)(
        arg1);
       break;
    case 111: ret = reinterpret_cast<syscall_func2>(sys_syscall_test_2)(
        arg1,
        arg2);
       break;
    case 112: ret = reinterpret_cast<syscall_func3>(sys_syscall_test_3)(
        arg1,
        arg2,
        arg3);
       break;
    case 113: ret = reinterpret_cast<syscall_func4>(sys_syscall_test_4)(
        arg1,
        arg2,
        arg3,
        arg4);
       break;
    case 114: ret = reinterpret_cast<syscall_func5>(sys_syscall_test_5)(
        arg1,
        arg2,
        arg3,
        arg4,
        arg5);
       break;
    case 115: ret = reinterpret_cast<syscall_func6>(sys_syscall_test_6)(
        arg1,
        arg2,
        arg3,
        arg4,
        arg5,
        arg6);
       break;
    case 116: ret = reinterpret_cast<syscall_func7>(sys_syscall_test_7)(
        arg1,
        arg2,
        arg3,
        arg4,
        arg5,
        arg6,
        arg7);
       break;
    case 117: ret = reinterpret_cast<syscall_func8>(sys_syscall_test_8)(
        arg1,
        arg2,
        arg3,
        arg4,
        arg5,
        arg6,
        arg7,
        arg8);
       break;

