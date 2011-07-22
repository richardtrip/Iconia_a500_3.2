#ifndef ___LSC_FROM_EC_H__
#define ___LSC_FROM_EC_H__

#define LSC_LINE  117

// this structure should be the same as
// "struct sensor_reg" in yuv5_init_tab_picasso.h
struct sensor_reg_copy {
	u8  op;
	u16 addr;
	u16 val;
};

extern struct sensor_reg_copy EC_D65_patch_ram_table[LSC_LINE+1];
extern struct sensor_reg_copy EC_CWF_patch_ram_table[LSC_LINE+1];
extern int lsc_from_ec_status;

#endif // __LSC_FROM_EC_H__
