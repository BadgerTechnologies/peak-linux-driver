#include <linux/module.h>
#include <linux/vermagic.h>
#include <linux/compiler.h>

MODULE_INFO(vermagic, VERMAGIC_STRING);

__visible struct module __this_module
__attribute__((section(".gnu.linkonce.this_module"))) = {
	.name = KBUILD_MODNAME,
	.init = init_module,
#ifdef CONFIG_MODULE_UNLOAD
	.exit = cleanup_module,
#endif
	.arch = MODULE_ARCH_INIT,
};

static const struct modversion_info ____versions[]
__used
__attribute__((section("__versions"))) = {
	{ 0x683cfe8d, __VMLINUX_SYMBOL_STR(module_layout) },
	{ 0x94e24fcd, __VMLINUX_SYMBOL_STR(pcmcia_dev_present) },
	{ 0x6bc3fbc0, __VMLINUX_SYMBOL_STR(__unregister_chrdev) },
	{ 0x2d3385d3, __VMLINUX_SYMBOL_STR(system_wq) },
	{ 0x5e25804, __VMLINUX_SYMBOL_STR(__request_region) },
	{ 0xba4d6f8c, __VMLINUX_SYMBOL_STR(class_remove_file_ns) },
	{ 0x7875fbf2, __VMLINUX_SYMBOL_STR(kmalloc_caches) },
	{ 0xd2b09ce5, __VMLINUX_SYMBOL_STR(__kmalloc) },
	{ 0x311ba5ac, __VMLINUX_SYMBOL_STR(pcmcia_enable_device) },
	{ 0xf6d8d765, __VMLINUX_SYMBOL_STR(single_open) },
	{ 0xc364ae22, __VMLINUX_SYMBOL_STR(iomem_resource) },
	{ 0x48fcdabb, __VMLINUX_SYMBOL_STR(pcmcia_register_driver) },
	{ 0x5dd57f0a, __VMLINUX_SYMBOL_STR(usb_init_urb) },
	{ 0xd329538c, __VMLINUX_SYMBOL_STR(single_release) },
	{ 0x6e3ce7f4, __VMLINUX_SYMBOL_STR(usb_reset_endpoint) },
	{ 0x70d7563d, __VMLINUX_SYMBOL_STR(pci_disable_device) },
	{ 0x72d389ac, __VMLINUX_SYMBOL_STR(i2c_transfer) },
	{ 0x20000329, __VMLINUX_SYMBOL_STR(simple_strtoul) },
	{ 0x6b06fdce, __VMLINUX_SYMBOL_STR(delayed_work_timer_fn) },
	{ 0xb1903e77, __VMLINUX_SYMBOL_STR(seq_printf) },
	{ 0xc87c1f84, __VMLINUX_SYMBOL_STR(ktime_get) },
	{ 0x974777c7, __VMLINUX_SYMBOL_STR(usb_kill_urb) },
	{ 0xacc9c85e, __VMLINUX_SYMBOL_STR(remove_proc_entry) },
	{ 0xb96273e1, __VMLINUX_SYMBOL_STR(usb_reset_configuration) },
	{ 0x8e2277a0, __VMLINUX_SYMBOL_STR(parport_find_base) },
	{ 0x50f0a941, __VMLINUX_SYMBOL_STR(__register_chrdev) },
	{ 0x55680635, __VMLINUX_SYMBOL_STR(driver_for_each_device) },
	{ 0xeae3dfd6, __VMLINUX_SYMBOL_STR(__const_udelay) },
	{ 0xfdec9ed1, __VMLINUX_SYMBOL_STR(pci_release_regions) },
	{ 0x8fdf772a, __VMLINUX_SYMBOL_STR(init_timer_key) },
	{ 0x3a4de076, __VMLINUX_SYMBOL_STR(cancel_delayed_work_sync) },
	{ 0x1afab522, __VMLINUX_SYMBOL_STR(mutex_unlock) },
	{ 0xfb0456ee, __VMLINUX_SYMBOL_STR(pci_bus_write_config_word) },
	{ 0xdb9be713, __VMLINUX_SYMBOL_STR(device_create_with_groups) },
	{ 0x91715312, __VMLINUX_SYMBOL_STR(sprintf) },
	{ 0x835dcf95, __VMLINUX_SYMBOL_STR(pcmcia_request_io) },
	{ 0x47275ef0, __VMLINUX_SYMBOL_STR(seq_read) },
	{ 0x7d11c268, __VMLINUX_SYMBOL_STR(jiffies) },
	{ 0x4069ca73, __VMLINUX_SYMBOL_STR(mutex_trylock) },
	{ 0x9e88526, __VMLINUX_SYMBOL_STR(__init_waitqueue_head) },
	{ 0xdea33bce, __VMLINUX_SYMBOL_STR(dma_get_required_mask) },
	{ 0x4f8b5ddb, __VMLINUX_SYMBOL_STR(_copy_to_user) },
	{ 0x50c045ea, __VMLINUX_SYMBOL_STR(param_ops_charp) },
	{ 0x65d462c4, __VMLINUX_SYMBOL_STR(pci_set_master) },
	{ 0x3f5c6471, __VMLINUX_SYMBOL_STR(del_timer_sync) },
	{ 0xfb578fc5, __VMLINUX_SYMBOL_STR(memset) },
	{ 0x150ad92b, __VMLINUX_SYMBOL_STR(ioport_resource) },
	{ 0x3622ab77, __VMLINUX_SYMBOL_STR(device_del) },
	{ 0xa31df149, __VMLINUX_SYMBOL_STR(pci_iounmap) },
	{ 0xcff815dc, __VMLINUX_SYMBOL_STR(dev_err) },
	{ 0x1916e38c, __VMLINUX_SYMBOL_STR(_raw_spin_unlock_irqrestore) },
	{ 0x2270cd45, __VMLINUX_SYMBOL_STR(current_task) },
	{ 0x37befc70, __VMLINUX_SYMBOL_STR(jiffies_to_msecs) },
	{ 0xa92ce54c, __VMLINUX_SYMBOL_STR(usb_deregister) },
	{ 0x5ad8164f, __VMLINUX_SYMBOL_STR(__mutex_init) },
	{ 0x27e1a049, __VMLINUX_SYMBOL_STR(printk) },
	{ 0xac8ed9ed, __VMLINUX_SYMBOL_STR(sysfs_remove_file_from_group) },
	{ 0xdef1ce2d, __VMLINUX_SYMBOL_STR(parport_unregister_device) },
	{ 0x641e3296, __VMLINUX_SYMBOL_STR(usb_set_interface) },
	{ 0xa1c76e0a, __VMLINUX_SYMBOL_STR(_cond_resched) },
	{ 0x9166fada, __VMLINUX_SYMBOL_STR(strncpy) },
	{ 0x1bbd1acd, __VMLINUX_SYMBOL_STR(usb_control_msg) },
	{ 0x16305289, __VMLINUX_SYMBOL_STR(warn_slowpath_null) },
	{ 0x6d55026b, __VMLINUX_SYMBOL_STR(mutex_lock) },
	{ 0x85825898, __VMLINUX_SYMBOL_STR(add_timer) },
	{ 0xb238f1de, __VMLINUX_SYMBOL_STR(parport_claim) },
	{ 0x2072ee9b, __VMLINUX_SYMBOL_STR(request_threaded_irq) },
	{ 0xfe7f53e2, __VMLINUX_SYMBOL_STR(pcmcia_loop_config) },
	{ 0x51aae4bf, __VMLINUX_SYMBOL_STR(parport_release) },
	{ 0x12f33d5a, __VMLINUX_SYMBOL_STR(arch_dma_alloc_attrs) },
	{ 0xf8d45a88, __VMLINUX_SYMBOL_STR(i2c_del_adapter) },
	{ 0x1ed19819, __VMLINUX_SYMBOL_STR(usb_submit_urb) },
	{ 0x78764f4e, __VMLINUX_SYMBOL_STR(pv_irq_ops) },
	{ 0x42c8de35, __VMLINUX_SYMBOL_STR(ioremap_nocache) },
	{ 0x12f103c2, __VMLINUX_SYMBOL_STR(pci_bus_read_config_word) },
	{ 0x9d378f70, __VMLINUX_SYMBOL_STR(queue_delayed_work_on) },
	{ 0xdb7305a1, __VMLINUX_SYMBOL_STR(__stack_chk_fail) },
	{ 0xdd944bc, __VMLINUX_SYMBOL_STR(usb_reset_device) },
	{ 0x44447fc7, __VMLINUX_SYMBOL_STR(parport_register_device) },
	{ 0xd62c833f, __VMLINUX_SYMBOL_STR(schedule_timeout) },
	{ 0x1000e51, __VMLINUX_SYMBOL_STR(schedule) },
	{ 0xa202a8e5, __VMLINUX_SYMBOL_STR(kmalloc_order_trace) },
	{ 0xa109df4c, __VMLINUX_SYMBOL_STR(usb_clear_halt) },
	{ 0xbdfb6dbb, __VMLINUX_SYMBOL_STR(__fentry__) },
	{ 0x4665810e, __VMLINUX_SYMBOL_STR(class_create_file_ns) },
	{ 0x8d15114a, __VMLINUX_SYMBOL_STR(__release_region) },
	{ 0x4e2b609c, __VMLINUX_SYMBOL_STR(pci_enable_msi_range) },
	{ 0xf3fdd38, __VMLINUX_SYMBOL_STR(pci_unregister_driver) },
	{ 0xccca000a, __VMLINUX_SYMBOL_STR(kmem_cache_alloc_trace) },
	{ 0x680ec266, __VMLINUX_SYMBOL_STR(_raw_spin_lock_irqsave) },
	{ 0x6b9e0bc5, __VMLINUX_SYMBOL_STR(param_ops_byte) },
	{ 0xa6bbd805, __VMLINUX_SYMBOL_STR(__wake_up) },
	{ 0xf6ebc03b, __VMLINUX_SYMBOL_STR(net_ratelimit) },
	{ 0x2207a57f, __VMLINUX_SYMBOL_STR(prepare_to_wait_event) },
	{ 0x4f68e5c9, __VMLINUX_SYMBOL_STR(do_gettimeofday) },
	{ 0x4752cef8, __VMLINUX_SYMBOL_STR(proc_create_data) },
	{ 0xbc78ef3c, __VMLINUX_SYMBOL_STR(seq_lseek) },
	{ 0x37a0cba, __VMLINUX_SYMBOL_STR(kfree) },
	{ 0x69acdf38, __VMLINUX_SYMBOL_STR(memcpy) },
	{ 0xeca0f12f, __VMLINUX_SYMBOL_STR(pci_request_regions) },
	{ 0x2d3336bb, __VMLINUX_SYMBOL_STR(param_array_ops) },
	{ 0x7286bd43, __VMLINUX_SYMBOL_STR(pci_disable_msi) },
	{ 0xfce6add6, __VMLINUX_SYMBOL_STR(dma_supported) },
	{ 0xedc03953, __VMLINUX_SYMBOL_STR(iounmap) },
	{ 0x677ac324, __VMLINUX_SYMBOL_STR(pcmcia_unregister_driver) },
	{ 0xbef9803c, __VMLINUX_SYMBOL_STR(__pci_register_driver) },
	{ 0x6957d126, __VMLINUX_SYMBOL_STR(usb_register_driver) },
	{ 0xcbdc8014, __VMLINUX_SYMBOL_STR(class_destroy) },
	{ 0xf08242c2, __VMLINUX_SYMBOL_STR(finish_wait) },
	{ 0x626f35ee, __VMLINUX_SYMBOL_STR(dev_warn) },
	{ 0x6e215f66, __VMLINUX_SYMBOL_STR(sysfs_add_file_to_group) },
	{ 0xd632c4b4, __VMLINUX_SYMBOL_STR(i2c_bit_add_bus) },
	{ 0x28318305, __VMLINUX_SYMBOL_STR(snprintf) },
	{ 0xb0e602eb, __VMLINUX_SYMBOL_STR(memmove) },
	{ 0x5745b407, __VMLINUX_SYMBOL_STR(pci_iomap) },
	{ 0xb6ccfec7, __VMLINUX_SYMBOL_STR(param_ops_ushort) },
	{ 0x4947a6d6, __VMLINUX_SYMBOL_STR(pcmcia_disable_device) },
	{ 0xda9d58fa, __VMLINUX_SYMBOL_STR(pci_enable_device) },
	{ 0x4f6b400b, __VMLINUX_SYMBOL_STR(_copy_from_user) },
	{ 0xab2f127e, __VMLINUX_SYMBOL_STR(param_ops_uint) },
	{ 0x47b440b5, __VMLINUX_SYMBOL_STR(__class_create) },
	{ 0x6db51ac7, __VMLINUX_SYMBOL_STR(dma_ops) },
	{ 0x88db9f48, __VMLINUX_SYMBOL_STR(__check_object_size) },
	{ 0xf20dabd8, __VMLINUX_SYMBOL_STR(free_irq) },
};

static const char __module_depends[]
__used
__attribute__((section(".modinfo"))) =
"depends=pcmcia,parport,i2c-algo-bit";

MODULE_ALIAS("pcmcia:m0377c0001f*fn*pfn*pa*pb*pc*pd*");
MODULE_ALIAS("pci:v0000001Cd00000001sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v0000001Cd00000003sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v0000001Cd00000004sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v0000001Cd00000005sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v0000001Cd00000006sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v0000001Cd00000007sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v0000001Cd00000008sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v0000001Cd00000002sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v0000001Cd0000000Asv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v0000001Cd00000009sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v0000001Cd00000010sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v0000001Cd00000013sv*sd*bc*sc*i*");
MODULE_ALIAS("usb:v0C72p000Cd*dc*dsc*dp*ic*isc*ip*in*");
MODULE_ALIAS("usb:v0C72p000Dd*dc*dsc*dp*ic*isc*ip*in*");
MODULE_ALIAS("usb:v0C72p0012d*dc*dsc*dp*ic*isc*ip*in*");
MODULE_ALIAS("usb:v0C72p0011d*dc*dsc*dp*ic*isc*ip*in*");

MODULE_INFO(srcversion, "7ABE64F98B48F2890E35E35");
