//
// Author: Wolfgang Spraul
//
// This is free and unencumbered software released into the public domain.
// For details see the UNLICENSE file at the root of the source tree.
//

#include <time.h>

#include "model.h"
#include "floorplan.h"
#include "control.h"

time_t g_start_time;
#define TIME()		(time(0)-g_start_time)
#define TIMESTAMP()	printf("O timestamp %lld\n", (long long) TIME())
#define MEMUSAGE()	printf("O memusage %i\n", get_vm_mb());
#define TIME_AND_MEM()	TIMESTAMP(); MEMUSAGE()

#define AUTOTEST_TMP_DIR	"autotest.tmp"

struct test_state
{
	struct fpga_model* model;
	// test filenames are: tmp_dir/autotest_<base_name>_<diff_counter>.???
	char tmp_dir[256];
	char base_name[256];
	int next_diff_counter;
};

static int dump_file(const char* path)
{
	char line[1024];
	FILE* f;

	printf("\n");
	printf("O begin dump %s\n", path);
	f = fopen(path, "r");
	EXIT(!f);
	while (fgets(line, sizeof(line), f)) {
		if (!strncmp(line, "--- ", 4)
		    || !strncmp(line, "+++ ", 4)
		    || !strncmp(line, "@@ ", 3))
			continue;
		printf(line);
	}
	fclose(f);
	printf("O end dump %s\n", path);
	printf("\n");
	return 0;
}

static int diff_start(struct test_state* tstate, const char* base_name)
{
	strcpy(tstate->base_name, base_name);
	tstate->next_diff_counter = 1;
	return 0;
}

static int diff_printf(struct test_state* tstate)
{
	char path[1024], tmp[1024], prior_fp[1024];
	int path_base;
	FILE* dest_f = 0;
	int rc;

	snprintf(path, sizeof(path), "%s/autotest_%s_%04i", tstate->tmp_dir,
		tstate->base_name, tstate->next_diff_counter);
	path_base = strlen(path);
	if (tstate->next_diff_counter == 1)
		strcpy(prior_fp, "/dev/null");
	else {
		snprintf(prior_fp, sizeof(prior_fp), "%s/autotest_%s_%04i.fp",
			tstate->tmp_dir, tstate->base_name,
			tstate->next_diff_counter-1);
	}

	strcpy(&path[path_base], ".fp");
	dest_f = fopen(path, "w");
	if (!dest_f) FAIL(errno);

	rc = printf_devices(dest_f, tstate->model, /*config_only*/ 1);
	if (rc) FAIL(rc);
	rc = printf_switches(dest_f, tstate->model, /*enabled_only*/ 1);
	if (rc) FAIL(rc);

	fclose(dest_f);
	dest_f = 0;
	path[path_base] = 0;
	snprintf(tmp, sizeof(tmp), "./autotest_diff.sh %s %s.fp >%s.log 2>&1",
		prior_fp, path, path);
	rc = system(tmp);
	if (rc) FAIL(rc);

	strcpy(&path[path_base], ".diff");
	rc = dump_file(path);
	if (rc) FAIL(rc);

	tstate->next_diff_counter++;
	return 0;
fail:
	if (dest_f) fclose(dest_f);
	return rc;
}

int main(int argc, char** argv)
{
	struct fpga_model model;
	struct fpga_device* P46_dev, *P48_dev, *logic_dev;
	int P46_y, P46_x, P46_idx, P48_y, P48_x, P48_idx, rc;
	struct test_state tstate;
	struct switch_to_yx switch_to;

	printf("\n");
	printf("O fpgatools automatic test suite. Be welcome and be "
			"our guest. namo namaha.\n");
	printf("\n");
	printf("O Time measured in seconds from 0.\n");
	g_start_time = time(0);
	TIMESTAMP();
	printf("O Memory usage reported in megabytes.\n");
	MEMUSAGE();

	printf("O Building memory model...\n");
	if ((rc = fpga_build_model(&model, XC6SLX9_ROWS, XC6SLX9_COLUMNS,
			XC6SLX9_LEFT_WIRING, XC6SLX9_RIGHT_WIRING)))
		goto fail;
	printf("O Done\n");
	TIME_AND_MEM();

	tstate.model = &model;
	strcpy(tstate.tmp_dir, AUTOTEST_TMP_DIR);
	mkdir(tstate.tmp_dir, S_IRWXU|S_IRWXG|S_IROTH|S_IXOTH);
	rc = diff_start(&tstate, "and");
	if (rc) FAIL(rc);
	
	// configure P46
	rc = fpga_find_iob(&model, "P46", &P46_y, &P46_x, &P46_idx);
	if (rc) FAIL(rc);
	P46_dev = fpga_dev(&model, P46_y, P46_x, DEV_IOB, P46_idx);
	if (!P46_dev) FAIL(EINVAL);
	P46_dev->instantiated = 1;
	strcpy(P46_dev->iob.istandard, IO_LVCMOS33);
	P46_dev->iob.bypass_mux = BYPASS_MUX_I;
	P46_dev->iob.I_mux = IMUX_I;

	// configure P48
	rc = fpga_find_iob(&model, "P48", &P48_y, &P48_x, &P48_idx);
	if (rc) FAIL(rc);
	P48_dev = fpga_dev(&model, P48_y, P48_x, DEV_IOB, P48_idx);
	if (!P48_dev) FAIL(EINVAL);
	P48_dev->instantiated = 1;
	strcpy(P48_dev->iob.ostandard, IO_LVCMOS33);
	P48_dev->iob.drive_strength = 12;
	P48_dev->iob.O_used = 1;
	P48_dev->iob.slew = SLEW_SLOW;
	P48_dev->iob.suspend = SUSP_3STATE;

	// configure logic
	logic_dev = fpga_dev(&model, /*y*/ 68, /*x*/ 13, DEV_LOGIC, DEV_LOGX);
	if (!logic_dev) FAIL(EINVAL);
	logic_dev->instantiated = 1;
	logic_dev->logic.D_used = 1;
	rc = fpga_set_lut(&model, logic_dev, D6_LUT, "A3", ZTERM);
	if (rc) FAIL(rc);

	rc = diff_printf(&tstate);
	if (rc) FAIL(rc);

	printf("P46 I pinw %s\n", strarray_lookup(&model.str, P46_dev->iob.pinw_out_I));

	switch_to.yx_req = YX_DEV_ILOGIC;
	switch_to.flags = SWTO_YX_DEF;
	switch_to.model = &model;
	switch_to.y = P46_y;
	switch_to.x = P46_x;
	switch_to.start_switch = P46_dev->iob.pinw_out_I;
	rc = fpga_switch_to_yx(&switch_to);
	if (rc) FAIL(rc);
	printf("%s\n", fmt_swchain(&model, switch_to.y, switch_to.x, switch_to.chain, switch_to.chain_size));

	switch_to.yx_req = YX_ROUTING_TILE;
	switch_to.flags = SWTO_YX_DEF;
	switch_to.y = switch_to.dest_y;
	switch_to.x = switch_to.dest_x;
	switch_to.start_switch = switch_to.dest_connpt;
	rc = fpga_switch_to_yx(&switch_to);
	if (rc) FAIL(rc);
	printf("%s\n", fmt_swchain(&model, switch_to.y, switch_to.x, switch_to.chain, switch_to.chain_size));

	switch_to.yx_req = YX_ROUTING_TO_FABLOGIC;
	switch_to.flags = SWTO_YX_CLOSEST;
	switch_to.y = switch_to.dest_y;
	switch_to.x = switch_to.dest_x;
	switch_to.start_switch = switch_to.dest_connpt;
	rc = fpga_switch_to_yx(&switch_to);
	if (rc) FAIL(rc);
	printf("%s\n", fmt_swchain(&model, switch_to.y, switch_to.x, switch_to.chain, switch_to.chain_size));

	// next: YX_DEV_LOGIC

	printf("P48 O pinw %s\n", strarray_lookup(&model.str, P48_dev->iob.pinw_in_O));

	printf("\n");
	printf("O Test suite completed.\n");
	TIME_AND_MEM();
	printf("\n");
	return EXIT_SUCCESS;
fail:
	return rc;
}
