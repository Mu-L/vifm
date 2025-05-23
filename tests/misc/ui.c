#include <stic.h>

#include <stdlib.h> /* free() */
#include <string.h> /* strcpy() strdup() */

#include <test-utils.h>

#include "../../src/cfg/config.h"
#include "../../src/ui/color_scheme.h"
#include "../../src/ui/colored_line.h"
#include "../../src/ui/column_view.h"
#include "../../src/ui/fileview.h"
#include "../../src/ui/tabs.h"
#include "../../src/ui/statusline.h"
#include "../../src/ui/ui.h"
#include "../../src/utils/str.h"
#include "../../src/cmd_core.h"
#include "../../src/filelist.h"

#include "utils.h"

static void check_tab_title(const tab_info_t *tab_info, const char text[]);
static char * identity(const char path[]);

SETUP_ONCE()
{
	cfg.shorten_title_paths = 0;
	cfg.tail_tab_line_paths = 0;
}

SETUP()
{
	conf_setup();
	view_setup(&lwin);
}

TEARDOWN()
{
	conf_teardown();
	view_teardown(&lwin);
}

TEST(cterm_color_overlapping)
{
	col_attr_t color = {
		.fg = -1,
		.bg = -1,
		.attr = -1,
		.combine_attrs = 0
	};

	col_attr_t admixture1 = {
		.fg = 10,
		.bg = 11,
		.attr = A_BOLD,
		.combine_attrs = 1
	};
	cs_overlap_colors(&color, &admixture1);
	assert_int_equal(10, color.fg);
	assert_int_equal(11, color.bg);
	assert_int_equal(A_BOLD, color.attr);

	col_attr_t admixture2 = {
		.fg = 20,
		.bg = 22,
		.attr = A_REVERSE,
		.combine_attrs = 1
	};
	cs_overlap_colors(&color, &admixture2);
	assert_int_equal(20, color.fg);
	assert_int_equal(22, color.bg);
	assert_int_equal(A_REVERSE, color.attr);
}

TEST(cterm_color_mixing)
{
	col_attr_t color = {
		.fg = -1,
		.bg = -1,
		.attr = -1,
		.combine_attrs = 0
	};

	col_attr_t admixture1 = {
		.fg = 10,
		.bg = 11,
		.attr = A_BOLD,
		.combine_attrs = 1
	};
	cs_mix_colors(&color, &admixture1);
	assert_int_equal(10, color.fg);
	assert_int_equal(11, color.bg);
	assert_int_equal(A_BOLD, color.attr);

	col_attr_t admixture2 = {
		.fg = 20,
		.bg = 22,
		.attr = A_REVERSE,
		.combine_attrs = 1
	};
	cs_mix_colors(&color, &admixture2);
	assert_int_equal(20, color.fg);
	assert_int_equal(22, color.bg);
	assert_int_equal(A_BOLD | A_REVERSE, color.attr);
}

TEST(gui_color_overlapping)
{
	curr_stats.direct_color = 1;

	col_attr_t color = {
		.fg = -1,
		.bg = -1,
		.attr = -1,
		.combine_attrs = 0
	};

	col_attr_t admixture1 = {
		.gui_set = 1,
		.gui_fg = 0xabcdef,
		.gui_bg = 0x123456,
		.gui_attr = A_BOLD,
		.combine_gui_attrs = 1
	};
	cs_overlap_colors(&color, &admixture1);
	assert_true(color.gui_set);
	assert_int_equal(0xabcdef, color.gui_fg);
	assert_int_equal(0x123456, color.gui_bg);
	assert_int_equal(A_BOLD, color.gui_attr);

	col_attr_t admixture2 = {
		.gui_set = 1,
		.gui_fg = 0xfedcba,
		.gui_bg = 0x654321,
		.gui_attr = A_REVERSE,
		.combine_gui_attrs = 1
	};
	cs_overlap_colors(&color, &admixture2);
	assert_true(color.gui_set);
	assert_int_equal(0xfedcba, color.gui_fg);
	assert_int_equal(0x654321, color.gui_bg);
	assert_int_equal(A_REVERSE, color.gui_attr);

	curr_stats.direct_color = 0;
}

TEST(gui_color_mixing)
{
	curr_stats.direct_color = 1;

	col_attr_t color = {
		.fg = -1,
		.bg = -1,
		.attr = -1,
		.combine_attrs = 0
	};

	col_attr_t admixture1 = {
		.gui_set = 1,
		.gui_fg = 0xabcdef,
		.gui_bg = 0x123456,
		.gui_attr = A_BOLD,
		.combine_gui_attrs = 1
	};
	cs_mix_colors(&color, &admixture1);
	assert_true(color.gui_set);
	assert_int_equal(0xabcdef, color.gui_fg);
	assert_int_equal(0x123456, color.gui_bg);
	assert_int_equal(A_BOLD, color.gui_attr);

	col_attr_t admixture2 = {
		.gui_set = 1,
		.gui_fg = 0xfedcba,
		.gui_bg = 0x654321,
		.gui_attr = A_REVERSE,
		.combine_gui_attrs = 1
	};
	cs_mix_colors(&color, &admixture2);
	assert_true(color.gui_set);
	assert_int_equal(0xfedcba, color.gui_fg);
	assert_int_equal(0x654321, color.gui_bg);
	assert_int_equal(A_BOLD | A_REVERSE, color.gui_attr);

	curr_stats.direct_color = 0;
}

TEST(cterm_to_gui_color)
{
	curr_stats.direct_color = 1;

	col_attr_t color = {
		.fg = 8,
		.bg = 9,
		.attr = -1,
		.combine_attrs = 0
	};

	/* Mixing is done just to trigger the conversion. */
	col_attr_t admixture = {
		.fg = -1,
		.bg = -1,
		.attr = -1,
		.combine_attrs = 0
	};
	cs_mix_colors(&color, &admixture);

	assert_true(color.gui_set);
	assert_int_equal(0x808080, color.gui_fg);
	assert_int_equal(0xff0000, color.gui_bg);
	assert_int_equal(-1, color.gui_attr);
}

TEST(make_tab_title_uses_name_if_present_and_no_format)
{
	update_string(&cfg.tab_label, "");
	tab_info_t tab_info = { .view = &lwin, .name = "name", };
	check_tab_title(&tab_info, "name");
}

TEST(make_tab_title_uses_path_if_name_is_missing_and_no_format)
{
	update_string(&cfg.tab_label, "");
	strcpy(lwin.curr_dir, "/lpath");
	tab_info_t tab_info = { .view = &lwin, .name = NULL, };
	check_tab_title(&tab_info, "/lpath");
}

TEST(make_tab_title_uses_format_in_regular_view)
{
	update_string(&cfg.tab_label, "tail:%p:t");
	strcpy(lwin.curr_dir, "/lpath/ltail");
	tab_info_t tab_info = { .view = &lwin, .name = NULL, };
	check_tab_title(&tab_info, "tail:ltail");
}

TEST(make_tab_title_uses_format_in_custom_view)
{
	make_abs_path(lwin.curr_dir, sizeof(lwin.curr_dir), TEST_DATA_PATH, "", NULL);
	flist_custom_start(&lwin, "test");
	flist_custom_add(&lwin, "existing-files/a");
	assert_true(flist_custom_finish(&lwin, CV_REGULAR, 0) == 0);

	update_string(&cfg.tab_label, "!%c!%p:t");
	tab_info_t tab_info = { .view = &lwin, .name = NULL, };
	check_tab_title(&tab_info, "!test!test-data");
}

TEST(make_tab_title_uses_format_after_custom_view)
{
	make_abs_path(lwin.curr_dir, sizeof(lwin.curr_dir), TEST_DATA_PATH, "", NULL);
	flist_custom_start(&lwin, "test");
	flist_custom_add(&lwin, "existing-files/a");
	assert_true(flist_custom_finish(&lwin, CV_REGULAR, 0) == 0);
	assert_success(navigate_to(&lwin, TEST_DATA_PATH));

	update_string(&cfg.tab_label, "!%c!");
	tab_info_t tab_info = { .view = &lwin, .name = NULL, };
	check_tab_title(&tab_info, "!!");
}

TEST(make_tab_title_handles_explore_mode_for_format)
{
	lwin.explore_mode = 1;

	make_abs_path(lwin.curr_dir, sizeof(lwin.curr_dir), TEST_DATA_PATH, "", NULL);
	flist_custom_start(&lwin, "test");
	flist_custom_add(&lwin, "existing-files/a");
	assert_true(flist_custom_finish(&lwin, CV_REGULAR, 0) == 0);

	update_string(&cfg.tab_label, "!%p:t!");
	tab_info_t tab_info = { .view = &lwin, .name = NULL, };
	check_tab_title(&tab_info, "!a!");

	lwin.explore_mode = 0;
}

TEST(make_tab_expands_tab_number)
{
	update_string(&cfg.tab_label, "%N");
	tab_info_t tab_info = { .view = &lwin, .name = "name", };
	check_tab_title(&tab_info, "1");
}

TEST(make_tab_expands_current_flag)
{
	update_string(&cfg.tab_label, "%1*%[%2*%C%]%N");
	tab_info_t tab_info = { .view = &lwin, .name = "name", };
	tab_title_info_t title_info;
	cline_t title;

	title_info = make_tab_title_info(&tab_info, &identity, 2, 1);
	title = make_tab_title(&title_info);
	dispose_tab_title_info(&title_info);

	assert_string_equal("3", title.line);
	assert_string_equal("c", title.attrs);
	cline_dispose(&title);

	title_info = make_tab_title_info(&tab_info, &identity, 1, 0);
	title = make_tab_title(&title_info);
	dispose_tab_title_info(&title_info);

	assert_string_equal("2", title.line);
	assert_string_equal("b", title.attrs);
	cline_dispose(&title);
}

TEST(tabline_formatting_smoke)
{
	curr_view = &lwin;
	other_view = &rwin;
	setup_grid(&lwin, 1, 1, 1);
	view_setup(&rwin);
	setup_grid(&rwin, 1, 1, 1);
	curr_stats.load_stage = 2;

	cfg.columns = 10;
	opt_handlers_setup();
	columns_setup_column(SK_BY_NAME);
	columns_setup_column(SK_BY_SIZE);

	tabs_new("long tab title", NULL);
	ui_view_title_update(&lwin);
	tabs_only(&lwin);

	opt_handlers_teardown();
	columns_teardown();

	curr_stats.load_stage = 0;
	view_teardown(&rwin);
	curr_view = NULL;
	other_view = NULL;
}

TEST(ui_stat_job_bar_remove_can_be_called_with_unknown_pointer)
{
	int i;
	for(i = 0; i < 10; ++i)
	{
		ui_stat_job_bar_remove(NULL);
	}
}

TEST(find_view_macro_works)
{
	const char *format = "%[%]%=%1*%{ignored %N}%[%-1t%N%N*%t%]%3*%{";
	const char *macros = "[]{t-";

	assert_string_equal("%N%N*%t%]%3*%{",
			find_view_macro(&format, macros, 'N', 0));
	assert_string_equal("%N*%t%]%3*%{", format);

	assert_string_equal("%N*%t%]%3*%{", find_view_macro(&format, macros, 'N', 0));
	assert_string_equal("*%t%]%3*%{", format);

	assert_string_equal(NULL, find_view_macro(&format, macros, 'N', 0));
	assert_string_equal("", format);

	assert_string_equal(NULL, find_view_macro(&format, macros, 'N', 0));
	assert_string_equal("", format);
}

TEST(ui_stat_height_works)
{
	cfg.display_statusline = 0;

	update_string(&cfg.status_line, "");
	assert_int_equal(0, ui_stat_height());

	cfg.display_statusline = 1;

	assert_int_equal(1, ui_stat_height());

	update_string(&cfg.status_line, "some %N stuff");
	assert_int_equal(2, ui_stat_height());

	update_string(&cfg.status_line, NULL);
	cfg.display_statusline = 0;
}

TEST(mouse_map_millerview)
{
	/*         left|mid|right
	 *      --------------------
	 * 0 row:   012|345|678
	 * 1 row:      | - |
	 */

	setup_grid(&lwin, /*column_count=*/1, /*list_rows=*/1, /*init=*/1);

	lwin.ls_view = 0;
	lwin.miller_view = 1;
	lwin.miller_ratios[0] = 1;
	lwin.miller_ratios[1] = 1;
	lwin.miller_ratios[2] = 1;
	lwin.top_line = 0;
	lwin.window_cols = 9;
	lwin.dir_entry[0].type = FT_DIR;

	assert_int_equal(FVM_LEAVE, fview_map_coordinates(&lwin, 1, 1));
	assert_int_equal(FVM_NONE, fview_map_coordinates(&lwin, 4, 1));
	assert_int_equal(FVM_OPEN, fview_map_coordinates(&lwin, 7, 1));

	cfg.extra_padding = 1;
	assert_int_equal(FVM_LEAVE, fview_map_coordinates(&lwin, 0, 0));
	assert_int_equal(FVM_LEAVE, fview_map_coordinates(&lwin, 1, 0));
	assert_int_equal(FVM_LEAVE, fview_map_coordinates(&lwin, 2, 0));
	assert_int_equal(0, fview_map_coordinates(&lwin, 3, 0));
	assert_int_equal(0, fview_map_coordinates(&lwin, 4, 0));
	assert_int_equal(0, fview_map_coordinates(&lwin, 5, 0));
	assert_int_equal(FVM_OPEN, fview_map_coordinates(&lwin, 6, 0));
	assert_int_equal(FVM_OPEN, fview_map_coordinates(&lwin, 7, 0));
	assert_int_equal(FVM_OPEN, fview_map_coordinates(&lwin, 8, 0));

	cfg.extra_padding = 0;
	assert_int_equal(FVM_LEAVE, fview_map_coordinates(&lwin, 0, 0));
	assert_int_equal(FVM_LEAVE, fview_map_coordinates(&lwin, 1, 0));
	assert_int_equal(FVM_LEAVE, fview_map_coordinates(&lwin, 2, 0));
	assert_int_equal(0, fview_map_coordinates(&lwin, 3, 0));
	assert_int_equal(0, fview_map_coordinates(&lwin, 4, 0));
	assert_int_equal(0, fview_map_coordinates(&lwin, 5, 0));
	assert_int_equal(FVM_OPEN, fview_map_coordinates(&lwin, 6, 0));
	assert_int_equal(FVM_OPEN, fview_map_coordinates(&lwin, 7, 0));
	assert_int_equal(FVM_OPEN, fview_map_coordinates(&lwin, 8, 0));
}

TEST(mouse_map_lsview)
{
	lwin.window_rows = 7;
	setup_grid(&lwin, /*column_count=*/2, /*list_rows=*/11, /*init=*/1);

	lwin.ls_view = 1;
	lwin.ls_transposed = 0;
	lwin.miller_view = 0;
	lwin.top_line = 0;
	lwin.max_filename_width = 2;

	/*         |0123|4567|89
	 *      -------------------
	 * 0 row:  | 00 | 01 | --
	 * 1 row:  | 02 | 03 | --
	 * 2 row:  | 04 | 05 | --
	 * 3 row:  | 06 | 07 | --
	 * 4 row:  | 08 | 09 | --
	 * 5 row:  | 10 | -- | --
	 * 6 row:  | -- | -- | --
	 */

	cfg.extra_padding = 1;
	lwin.window_cols = 10;
	assert_int_equal(0, fview_map_coordinates(&lwin, 0, 0));
	assert_int_equal(0, fview_map_coordinates(&lwin, 1, 0));
	assert_int_equal(0, fview_map_coordinates(&lwin, 2, 0));
	assert_int_equal(0, fview_map_coordinates(&lwin, 3, 0));
	assert_int_equal(1, fview_map_coordinates(&lwin, 4, 0));
	assert_int_equal(1, fview_map_coordinates(&lwin, 5, 0));
	assert_int_equal(1, fview_map_coordinates(&lwin, 6, 0));
	assert_int_equal(1, fview_map_coordinates(&lwin, 7, 0));
	assert_int_equal(6, fview_map_coordinates(&lwin, 0, 3));
	assert_int_equal(6, fview_map_coordinates(&lwin, 1, 3));
	assert_int_equal(6, fview_map_coordinates(&lwin, 2, 3));
	assert_int_equal(6, fview_map_coordinates(&lwin, 3, 3));
	assert_int_equal(FVM_NONE, fview_map_coordinates(&lwin, 8, 0));
	assert_int_equal(FVM_NONE, fview_map_coordinates(&lwin, 9, 0));
	assert_int_equal(FVM_NONE, fview_map_coordinates(&lwin, 4, 5));
	assert_int_equal(FVM_NONE, fview_map_coordinates(&lwin, 5, 5));
	assert_int_equal(FVM_NONE, fview_map_coordinates(&lwin, 6, 5));
	assert_int_equal(FVM_NONE, fview_map_coordinates(&lwin, 7, 5));
	assert_int_equal(FVM_NONE, fview_map_coordinates(&lwin, 2, 6));
	assert_int_equal(FVM_NONE, fview_map_coordinates(&lwin, 6, 6));

	/*         |012|345|67
	 *      ----------------
	 * 0 row:  |00 |01 |--
	 * 1 row:  |02 |03 |--
	 * 2 row:  |04 |05 |--
	 * 3 row:  |06 |07 |--
	 * 4 row:  |08 |09 |--
	 * 5 row:  |10 |-- |--
	 * 6 row:  |-- |-- |--
	 */

	cfg.extra_padding = 0;
	lwin.window_cols = 8;
	assert_int_equal(0, fview_map_coordinates(&lwin, 0, 0));
	assert_int_equal(0, fview_map_coordinates(&lwin, 1, 0));
	assert_int_equal(0, fview_map_coordinates(&lwin, 2, 0));
	assert_int_equal(1, fview_map_coordinates(&lwin, 3, 0));
	assert_int_equal(1, fview_map_coordinates(&lwin, 4, 0));
	assert_int_equal(1, fview_map_coordinates(&lwin, 5, 0));
	assert_int_equal(6, fview_map_coordinates(&lwin, 0, 3));
	assert_int_equal(6, fview_map_coordinates(&lwin, 1, 3));
	assert_int_equal(6, fview_map_coordinates(&lwin, 2, 3));
	assert_int_equal(FVM_NONE, fview_map_coordinates(&lwin, 6, 0));
	assert_int_equal(FVM_NONE, fview_map_coordinates(&lwin, 7, 0));
	assert_int_equal(FVM_NONE, fview_map_coordinates(&lwin, 4, 5));
	assert_int_equal(FVM_NONE, fview_map_coordinates(&lwin, 5, 5));
	assert_int_equal(FVM_NONE, fview_map_coordinates(&lwin, 6, 5));
	assert_int_equal(FVM_NONE, fview_map_coordinates(&lwin, 2, 6));
	assert_int_equal(FVM_NONE, fview_map_coordinates(&lwin, 6, 6));
}

TEST(mouse_map_tlsview)
{
	lwin.window_rows = 6;
	setup_grid(&lwin, /*column_count=*/2, /*list_rows=*/11, /*init=*/1);

	lwin.ls_view = 1;
	lwin.ls_transposed = 1;
	lwin.miller_view = 0;
	lwin.top_line = 0;
	lwin.max_filename_width = 2;

	/*         |0123|4567|89
	 *      -------------------
	 * 0 row:  | 00 | 06 | --
	 * 1 row:  | 01 | 07 | --
	 * 2 row:  | 02 | 08 | --
	 * 3 row:  | 03 | 09 | --
	 * 4 row:  | 04 | 10 | --
	 * 5 row:  | 05 | -- | --
	 */

	cfg.extra_padding = 1;
	lwin.window_cols = 10;
	assert_int_equal(0, fview_map_coordinates(&lwin, 0, 0));
	assert_int_equal(0, fview_map_coordinates(&lwin, 1, 0));
	assert_int_equal(0, fview_map_coordinates(&lwin, 2, 0));
	assert_int_equal(0, fview_map_coordinates(&lwin, 3, 0));
	assert_int_equal(6, fview_map_coordinates(&lwin, 4, 0));
	assert_int_equal(6, fview_map_coordinates(&lwin, 5, 0));
	assert_int_equal(6, fview_map_coordinates(&lwin, 6, 0));
	assert_int_equal(6, fview_map_coordinates(&lwin, 7, 0));
	assert_int_equal(3, fview_map_coordinates(&lwin, 0, 3));
	assert_int_equal(3, fview_map_coordinates(&lwin, 1, 3));
	assert_int_equal(3, fview_map_coordinates(&lwin, 2, 3));
	assert_int_equal(3, fview_map_coordinates(&lwin, 3, 3));
	assert_int_equal(FVM_NONE, fview_map_coordinates(&lwin, 8, 0));
	assert_int_equal(FVM_NONE, fview_map_coordinates(&lwin, 9, 0));
	assert_int_equal(FVM_NONE, fview_map_coordinates(&lwin, 4, 5));
	assert_int_equal(FVM_NONE, fview_map_coordinates(&lwin, 5, 5));
	assert_int_equal(FVM_NONE, fview_map_coordinates(&lwin, 6, 5));
	assert_int_equal(FVM_NONE, fview_map_coordinates(&lwin, 7, 5));

	/*         |012|345|67
	 *      ----------------
	 * 0 row:  |00 |06 |--
	 * 1 row:  |01 |07 |--
	 * 2 row:  |02 |08 |--
	 * 3 row:  |03 |09 |--
	 * 4 row:  |04 |10 |--
	 * 5 row:  |05 |-- |--
	 */

	cfg.extra_padding = 0;
	lwin.window_cols = 8;
	assert_int_equal(0, fview_map_coordinates(&lwin, 0, 0));
	assert_int_equal(0, fview_map_coordinates(&lwin, 1, 0));
	assert_int_equal(0, fview_map_coordinates(&lwin, 2, 0));
	assert_int_equal(6, fview_map_coordinates(&lwin, 3, 0));
	assert_int_equal(6, fview_map_coordinates(&lwin, 4, 0));
	assert_int_equal(6, fview_map_coordinates(&lwin, 5, 0));
	assert_int_equal(3, fview_map_coordinates(&lwin, 0, 3));
	assert_int_equal(3, fview_map_coordinates(&lwin, 1, 3));
	assert_int_equal(3, fview_map_coordinates(&lwin, 2, 3));
	assert_int_equal(FVM_NONE, fview_map_coordinates(&lwin, 6, 0));
	assert_int_equal(FVM_NONE, fview_map_coordinates(&lwin, 7, 0));
	assert_int_equal(FVM_NONE, fview_map_coordinates(&lwin, 4, 5));
	assert_int_equal(FVM_NONE, fview_map_coordinates(&lwin, 5, 5));
	assert_int_equal(FVM_NONE, fview_map_coordinates(&lwin, 6, 5));
}

TEST(prefix_len_is_reset_by_column_line_print)
{
	curr_view = &lwin;
	fview_setup();
	cmds_init();
	opt_handlers_setup();

	assert_success(load_tree(&lwin, TEST_DATA_PATH "/tree", NULL));
	assert_true(lwin.list_rows > 6);
	lwin.list_pos = 6;
	lwin.columns = columns_create();

	assert_success(cmds_dispatch("set viewcolumns={name},{ext}", &lwin,
				CIT_COMMAND));
	curr_stats.load_stage = 2;
	/* If this doesn't cause a crash or a memory issue, then there should be no
	 * bug related to tree prefix length computed for the {name} column being used
	 * for drawing {ext}. */
	fview_cursor_redraw(&lwin);
	curr_stats.load_stage = 0;

	curr_view = NULL;
	vle_cmds_reset();
	opt_handlers_teardown();
	columns_teardown();
}

static void
check_tab_title(const tab_info_t *tab_info, const char text[])
{
	tab_title_info_t title_info = make_tab_title_info(tab_info, &identity, 0, 0);
	cline_t title = make_tab_title(&title_info);
	dispose_tab_title_info(&title_info);

	assert_string_equal(text, title.line);
	cline_dispose(&title);
}

static char *
identity(const char path[])
{
	return (char *)path;
}

/* vim: set tabstop=2 softtabstop=2 shiftwidth=2 noexpandtab cinoptions-=(0 : */
/* vim: set cinoptions+=t0 filetype=c : */
