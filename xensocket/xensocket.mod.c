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
	{ 0x9d35aeec, __VMLINUX_SYMBOL_STR(module_layout) },
	{ 0x9dfa1224, __VMLINUX_SYMBOL_STR(sock_no_sendpage) },
	{ 0x59747866, __VMLINUX_SYMBOL_STR(sock_no_mmap) },
	{ 0x35b10f71, __VMLINUX_SYMBOL_STR(sock_no_getsockopt) },
	{ 0x29abae8a, __VMLINUX_SYMBOL_STR(sock_no_setsockopt) },
	{ 0x7bd64632, __VMLINUX_SYMBOL_STR(sock_no_ioctl) },
	{ 0xd1ab6e05, __VMLINUX_SYMBOL_STR(sock_no_poll) },
	{ 0x4aa1ab67, __VMLINUX_SYMBOL_STR(sock_no_getname) },
	{ 0x22408518, __VMLINUX_SYMBOL_STR(sock_no_socketpair) },
	{ 0x1766e12, __VMLINUX_SYMBOL_STR(proto_unregister) },
	{ 0x62737e1d, __VMLINUX_SYMBOL_STR(sock_unregister) },
	{ 0x6e96238e, __VMLINUX_SYMBOL_STR(sock_register) },
	{ 0xb5c65e33, __VMLINUX_SYMBOL_STR(proto_register) },
	{ 0x4f6b400b, __VMLINUX_SYMBOL_STR(_copy_from_user) },
	{ 0x83349334, __VMLINUX_SYMBOL_STR(copy_from_iter) },
	{ 0xba5a4e98, __VMLINUX_SYMBOL_STR(sk_free) },
	{ 0xf08242c2, __VMLINUX_SYMBOL_STR(finish_wait) },
	{ 0xd62c833f, __VMLINUX_SYMBOL_STR(schedule_timeout) },
	{ 0x3bb5114a, __VMLINUX_SYMBOL_STR(prepare_to_wait) },
	{ 0xc8b57c27, __VMLINUX_SYMBOL_STR(autoremove_wake_function) },
	{ 0x298f5f9, __VMLINUX_SYMBOL_STR(current_task) },
	{ 0xfe727411, __VMLINUX_SYMBOL_STR(get_phys_to_machine) },
	{ 0x3a7d80f9, __VMLINUX_SYMBOL_STR(xen_max_p2m_pfn) },
	{ 0x25f02c87, __VMLINUX_SYMBOL_STR(xen_p2m_addr) },
	{ 0x3362b03c, __VMLINUX_SYMBOL_STR(xen_p2m_size) },
	{ 0x2142697b, __VMLINUX_SYMBOL_STR(kmem_cache_alloc_trace) },
	{ 0x8a9809d6, __VMLINUX_SYMBOL_STR(kmalloc_caches) },
	{ 0xb6230f1f, __VMLINUX_SYMBOL_STR(gnttab_grant_foreign_access) },
	{ 0x55526907, __VMLINUX_SYMBOL_STR(xen_features) },
	{ 0x4c9d28b0, __VMLINUX_SYMBOL_STR(phys_base) },
	{ 0x93fca811, __VMLINUX_SYMBOL_STR(__get_free_pages) },
	{ 0x8b025881, __VMLINUX_SYMBOL_STR(sock_init_data) },
	{ 0xc3afd302, __VMLINUX_SYMBOL_STR(sk_alloc) },
	{ 0x4302d0eb, __VMLINUX_SYMBOL_STR(free_pages) },
	{ 0xedbc6f67, __VMLINUX_SYMBOL_STR(gnttab_end_foreign_access) },
	{ 0xd2b09ce5, __VMLINUX_SYMBOL_STR(__kmalloc) },
	{ 0xe41534ce, __VMLINUX_SYMBOL_STR(bind_evtchn_to_irqhandler) },
	{ 0x209ec764, __VMLINUX_SYMBOL_STR(xen_event_channel_op_compat) },
	{ 0x228b28ff, __VMLINUX_SYMBOL_STR(alloc_vm_area) },
	{ 0xdb7305a1, __VMLINUX_SYMBOL_STR(__stack_chk_fail) },
	{ 0x27e1a049, __VMLINUX_SYMBOL_STR(printk) },
	{ 0x10004855, __VMLINUX_SYMBOL_STR(free_vm_area) },
	{ 0x37a0cba, __VMLINUX_SYMBOL_STR(kfree) },
	{ 0x45d14bdf, __VMLINUX_SYMBOL_STR(hypercall_page) },
	{ 0xa6bbd805, __VMLINUX_SYMBOL_STR(__wake_up) },
	{ 0x4f8b5ddb, __VMLINUX_SYMBOL_STR(_copy_to_user) },
	{ 0xbdfb6dbb, __VMLINUX_SYMBOL_STR(__fentry__) },
};

static const char __module_depends[]
__used
__attribute__((section(".modinfo"))) =
"depends=";


MODULE_INFO(srcversion, "C29EE51464B81EA717029BF");
