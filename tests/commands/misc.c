#include <stic.h>

#include <unistd.h> /* chdir() rmdir() */

#include <stdio.h> /* remove() snprintf() */
#include <string.h> /* strcpy() strdup() */

#include <test-utils.h>

#include "../../src/cfg/config.h"
#include "../../src/compat/fs_limits.h"
#include "../../src/compat/os.h"
#include "../../src/lua/vlua.h"
#include "../../src/modes/menu.h"
#include "../../src/ui/statusbar.h"
#include "../../src/ui/ui.h"
#include "../../src/utils/dynarray.h"
#include "../../src/utils/fs.h"
#include "../../src/utils/macros.h"
#include "../../src/utils/path.h"
#include "../../src/utils/str.h"
#include "../../src/utils/string_array.h"
#include "../../src/cmd_core.h"
#include "../../src/compare.h"
#include "../../src/filelist.h"
#include "../../src/flist_hist.h"
#include "../../src/plugins.h"
#include "../../src/registers.h"
#include "../../src/running.h"
#include "../../src/status.h"
#include "../lua/asserts.h"

static char *saved_cwd;

static char cwd[PATH_MAX + 1];
static char sandbox[PATH_MAX + 1];
static char test_data[PATH_MAX + 1];

static void strings_list_is(const strlist_t expected, const strlist_t actual);

SETUP_ONCE()
{
	cfg.sizefmt.base = 1;

	assert_non_null(get_cwd(cwd, sizeof(cwd)));

	make_abs_path(sandbox, sizeof(sandbox), SANDBOX_PATH, "", cwd);
	make_abs_path(test_data, sizeof(test_data), TEST_DATA_PATH, "", cwd);
}

SETUP()
{
	view_setup(&lwin);
	view_setup(&rwin);

	curr_view = &lwin;
	other_view = &rwin;

	conf_setup();
	undo_setup();
	cmds_init();

	saved_cwd = save_cwd();
}

TEARDOWN()
{
	restore_cwd(saved_cwd);

	view_teardown(&lwin);
	view_teardown(&rwin);

	conf_teardown();
	vle_cmds_reset();
	undo_teardown();
}

TEST(cd_in_root_works)
{
	assert_success(chdir(test_data));

	strcpy(lwin.curr_dir, test_data);

	assert_false(is_root_dir(lwin.curr_dir));
	assert_success(cmds_dispatch("cd /", &lwin, CIT_COMMAND));
	assert_true(is_root_dir(lwin.curr_dir));
}

TEST(double_cd_uses_same_base_for_rel_paths)
{
	char path[PATH_MAX + 1];

	assert_success(chdir(test_data));

	strcpy(lwin.curr_dir, test_data);
	strcpy(rwin.curr_dir, "..");

	assert_success(cmds_dispatch("cd read rename", &lwin, CIT_COMMAND));

	snprintf(path, sizeof(path), "%s/read", test_data);
	assert_true(paths_are_equal(lwin.curr_dir, path));
	snprintf(path, sizeof(path), "%s/rename", test_data);
	assert_true(paths_are_equal(rwin.curr_dir, path));
}

TEST(symlinks_in_paths_are_not_resolved, IF(not_windows))
{
	char canonic_path[PATH_MAX + 1];
	char buf[PATH_MAX + 1];

	assert_success(os_mkdir(SANDBOX_PATH "/dir1", 0700));
	assert_success(os_mkdir(SANDBOX_PATH "/dir1/dir2", 0700));

	char src[PATH_MAX + 1], dst[PATH_MAX + 1];
	make_abs_path(src, sizeof(src), SANDBOX_PATH, "dir1/dir2", saved_cwd);
	make_abs_path(dst, sizeof(dst), SANDBOX_PATH, "dir-link", saved_cwd);
	assert_success(make_symlink(src, dst));

	assert_success(chdir(SANDBOX_PATH "/dir-link"));
	make_abs_path(buf, sizeof(buf), SANDBOX_PATH, "dir-link", saved_cwd);
	to_canonic_path(buf, "/fake-root", lwin.curr_dir,
			sizeof(lwin.curr_dir));
	to_canonic_path(sandbox, "/fake-root", canonic_path,
			sizeof(canonic_path));

	/* :mkdir */
	(void)cmds_dispatch("mkdir ../dir", &lwin, CIT_COMMAND);
	restore_cwd(saved_cwd);
	saved_cwd = save_cwd();
	assert_success(rmdir(SANDBOX_PATH "/dir"));

	/* :clone file name. */
	create_file(SANDBOX_PATH "/dir-link/file");
	populate_dir_list(&lwin, 1);
	(void)cmds_dispatch("clone ../file-clone", &lwin, CIT_COMMAND);
	restore_cwd(saved_cwd);
	saved_cwd = save_cwd();
	assert_success(remove(SANDBOX_PATH "/file-clone"));
	assert_success(remove(SANDBOX_PATH "/dir-link/file"));

	/* :colorscheme */
	make_abs_path(cfg.colors_dir, sizeof(cfg.colors_dir), TEST_DATA_PATH,
			"scripts/", saved_cwd);
	snprintf(buf, sizeof(buf), "colorscheme set-env %s/../dir-link/..",
			sandbox);
	assert_success(cmds_dispatch(buf, &lwin, CIT_COMMAND));
	cs_load_defaults();

	/* :cd */
	assert_success(cmds_dispatch("cd ../dir-link/..", &lwin, CIT_COMMAND));
	assert_string_equal(canonic_path, lwin.curr_dir);

	restore_cwd(saved_cwd);
	saved_cwd = save_cwd();
	assert_success(remove(SANDBOX_PATH "/dir-link"));
	assert_success(rmdir(SANDBOX_PATH "/dir1/dir2"));
	assert_success(rmdir(SANDBOX_PATH "/dir1"));
}

TEST(grep_command, IF(not_windows))
{
	opt_handlers_setup();

	replace_string(&cfg.shell, "/bin/sh");
	update_string(&cfg.shell_cmd_flag, "-c");

	assert_success(chdir(TEST_DATA_PATH "/scripts"));
	assert_non_null(get_cwd(lwin.curr_dir, sizeof(lwin.curr_dir)));

	assert_success(cmds_dispatch("set grepprg='grep -n -H -r %i %a %s %u'", &lwin,
				CIT_COMMAND));

	/* Nothing to repeat. */
	assert_failure(cmds_dispatch("grep", &lwin, CIT_COMMAND));

	assert_success(cmds_dispatch("grep command", &lwin, CIT_COMMAND));
	assert_int_equal(2, lwin.list_rows);
	assert_string_equal("Grep command", lwin.custom.title);

	/* Repeat last grep, but add inversion. */
	assert_success(cmds_dispatch("grep!", &lwin, CIT_COMMAND));
	assert_int_equal(5, lwin.list_rows);
	assert_string_equal("Grep command", lwin.custom.title);

	opt_handlers_teardown();
}

TEST(compare)
{
	opt_handlers_setup();
	create_file(SANDBOX_PATH "/file");

	to_canonic_path(SANDBOX_PATH, cwd, lwin.curr_dir, sizeof(lwin.curr_dir));

	/* The file is empty so nothing to do when "skipempty" is specified. */
	assert_success(cmds_dispatch("compare ofone skipempty", &lwin, CIT_COMMAND));
	assert_false(flist_custom_active(&lwin));

	/* Verify that later arguments override the former ones. */
	(void)cmds_dispatch("compare byname bysize bycontents listall listdups "
			"listunique ofboth ofone groupids grouppaths", &lwin, CIT_COMMAND);
	assert_true(flist_custom_active(&lwin));
	assert_int_equal(CV_REGULAR, lwin.custom.type);
	rn_leave(&lwin, /*levels=*/1);

	/* Can't toggle without !. */
	(void)cmds_dispatch("compare byname", &lwin, CIT_COMMAND);
	assert_int_equal(CF_GROUP_PATHS | CF_SHOW, lwin.custom.diff_cmp_flags);
	(void)cmds_dispatch("compare showdifferent", &lwin, CIT_COMMAND);
	assert_int_equal(CF_GROUP_PATHS | CF_SHOW, lwin.custom.diff_cmp_flags);
	rn_leave(&lwin, /*levels=*/1);

	/* No toggling. */
	(void)cmds_dispatch("compare! showdifferent", &lwin, CIT_COMMAND);
	assert_string_equal("Toggling requires active compare view", ui_sb_last());
	/* Verify that two-pane compare gets correct arguments. */
	make_abs_path(rwin.curr_dir, sizeof(rwin.curr_dir), TEST_DATA_PATH, "rename",
			cwd);
	(void)cmds_dispatch("compare byname withrcase withicase", &lwin, CIT_COMMAND);
	assert_true(flist_custom_active(&lwin));
	assert_true(flist_custom_active(&rwin));
	assert_int_equal(CT_NAME, lwin.custom.diff_cmp_type);
	assert_int_equal(LT_ALL, lwin.custom.diff_list_type);
	assert_int_equal(CF_GROUP_PATHS | CF_IGNORE_CASE | CF_SHOW,
			lwin.custom.diff_cmp_flags);
	/* Toggling. */
	(void)cmds_dispatch("compare! showidentical showdifferent", &lwin,
			CIT_COMMAND);
	assert_true(flist_custom_active(&lwin));
	assert_true(flist_custom_active(&rwin));
	assert_int_equal(CT_NAME, lwin.custom.diff_cmp_type);
	assert_int_equal(LT_ALL, lwin.custom.diff_list_type);
	assert_int_equal(CF_GROUP_PATHS | CF_IGNORE_CASE | CF_SHOW_UNIQUE_LEFT |
			CF_SHOW_UNIQUE_RIGHT, lwin.custom.diff_cmp_flags);
	/* Bad toggling. */
	(void)cmds_dispatch("compare! byname", &lwin, CIT_COMMAND);
	assert_int_equal(CF_GROUP_PATHS | CF_IGNORE_CASE | CF_SHOW_UNIQUE_LEFT |
			CF_SHOW_UNIQUE_RIGHT, lwin.custom.diff_cmp_flags);
	assert_string_equal("Unexpected property for toggling: byname", ui_sb_last());

	assert_success(chdir(cwd));
	assert_success(remove(SANDBOX_PATH "/file"));
	opt_handlers_teardown();
}

TEST(screen)
{
	assert_false(cfg.use_term_multiplexer);

	/* :screen toggles the option. */
	assert_success(cmds_dispatch("screen", &lwin, CIT_COMMAND));
	assert_true(cfg.use_term_multiplexer);
	assert_success(cmds_dispatch("screen", &lwin, CIT_COMMAND));
	assert_false(cfg.use_term_multiplexer);

	/* :screen! sets it to on. */
	assert_success(cmds_dispatch("screen!", &lwin, CIT_COMMAND));
	assert_true(cfg.use_term_multiplexer);
	assert_success(cmds_dispatch("screen!", &lwin, CIT_COMMAND));
	assert_true(cfg.use_term_multiplexer);

	cfg.use_term_multiplexer = 0;
}

TEST(hist_next_and_prev)
{
	/* Emulate proper history initialization (must happen after view
	 * initialization). */
	cfg_resize_histories(10);
	cfg_resize_histories(0);
	cfg_resize_histories(10);

	flist_hist_setup(&lwin, sandbox, ".", 0, 1);
	flist_hist_setup(&lwin, test_data, ".", 0, 1);

	assert_success(cmds_dispatch("histprev", &lwin, CIT_COMMAND));
	assert_true(paths_are_same(lwin.curr_dir, sandbox));
	assert_success(cmds_dispatch("histnext", &lwin, CIT_COMMAND));
	assert_true(paths_are_same(lwin.curr_dir, test_data));

	cfg_resize_histories(0);
}

TEST(keepsel_preserves_selection)
{
	init_view_list(&lwin);

	lwin.dir_entry[0].selected = 1;
	lwin.selected_files = 1;
	assert_failure(cmds_dispatch("echo 'hi'", &lwin, CIT_COMMAND));
	assert_int_equal(0, lwin.selected_files);
	assert_false(lwin.dir_entry[0].selected);

	lwin.dir_entry[0].selected = 1;
	lwin.selected_files = 1;
	assert_failure(cmds_dispatch("keepsel echo 'hi'", &lwin, CIT_COMMAND));
	assert_int_equal(1, lwin.selected_files);
	assert_true(lwin.dir_entry[0].selected);
}

TEST(echo_reports_all_errors)
{
	const char *expected;

	expected = "Expression is missing closing quote: \"hi\n"
	           "Invalid expression: \"hi";

	ui_sb_msg("");
	assert_failure(cmds_dispatch("echo \"hi", &lwin, CIT_COMMAND));
	assert_string_equal(expected, ui_sb_last());

	expected = "Expression is missing closing parenthesis: (1\n"
	           "Invalid expression: (1";

	ui_sb_msg("");
	assert_failure(cmds_dispatch("echo (1", &lwin, CIT_COMMAND));
	assert_string_equal(expected, ui_sb_last());

	char zeroes[8192] = "echo ";
	memset(zeroes + strlen(zeroes), '0', sizeof(zeroes) - (strlen(zeroes) + 1U));
	zeroes[sizeof(zeroes) - 1U] = '\0';

	ui_sb_msg("");
	assert_failure(cmds_dispatch(zeroes, &lwin, CIT_COMMAND));
	assert_true(strchr(ui_sb_last(), '\n') != NULL);
}

TEST(echo_without_arguments_prints_nothing)
{
	ui_sb_msg("");

	/* First, print some message to record it as the last one. */
	assert_failure(cmds_dispatch("echo 'previous'", &lwin, CIT_COMMAND));
	assert_string_equal("previous", ui_sb_last());

	/* Now, no message.  The last one could popup here. */
	assert_failure(cmds_dispatch("echo", &lwin, CIT_COMMAND));
	assert_string_equal("", ui_sb_last());
}

TEST(tree_command)
{
	strcpy(lwin.curr_dir, sandbox);

	/* Invalid input. */
	assert_failure(cmds_dispatch("tree nesting=0", &lwin, CIT_COMMAND));
	assert_false(flist_custom_active(&lwin));
	assert_string_equal("Invalid argument: nesting=0", ui_sb_last());
	assert_failure(cmds_dispatch("tree depth=0", &lwin, CIT_COMMAND));
	assert_false(flist_custom_active(&lwin));
	assert_string_equal("Invalid depth: 0", ui_sb_last());

	/* :tree enters tree mode. */
	assert_success(cmds_dispatch("tree", &lwin, CIT_COMMAND));
	assert_true(flist_custom_active(&lwin));
	assert_true(cv_tree(lwin.custom.type));

	/* Repeating :tree leaves view in tree mode. */
	assert_success(cmds_dispatch("tree", &lwin, CIT_COMMAND));
	assert_true(flist_custom_active(&lwin));
	assert_true(cv_tree(lwin.custom.type));

	/* :tree! can leave tree mode. */
	assert_success(cmds_dispatch("tree!", &lwin, CIT_COMMAND));
	assert_false(flist_custom_active(&lwin));

	/* :tree! can enter tree mode. */
	assert_success(cmds_dispatch("tree!", &lwin, CIT_COMMAND));
	assert_true(flist_custom_active(&lwin));
	assert_true(cv_tree(lwin.custom.type));

	/* Limited nesting. */

	char sub_path[PATH_MAX + 1];
	snprintf(sub_path, sizeof(sub_path), "%s/sub", sandbox);
	create_dir(sub_path);

	char sub_sub_path[PATH_MAX + 1];
	snprintf(sub_sub_path, sizeof(sub_sub_path), "%s/sub/sub", sandbox);
	create_dir(sub_sub_path);

	assert_success(cmds_dispatch("tree depth=1", &lwin, CIT_COMMAND));
	assert_true(flist_custom_active(&lwin));
	assert_true(cv_tree(lwin.custom.type));
	assert_int_equal(1, lwin.list_rows);

	remove_dir(sub_sub_path);
	remove_dir(sub_path);
}

TEST(regular_command)
{
	strcpy(lwin.curr_dir, sandbox);

	/* :tree enters tree mode. */
	assert_success(cmds_dispatch("tree", &lwin, CIT_COMMAND));
	assert_true(flist_custom_active(&lwin));
	assert_true(cv_tree(lwin.custom.type));

	/* :regular leaves tree mode. */
	assert_success(cmds_dispatch("regular", &lwin, CIT_COMMAND));
	assert_false(flist_custom_active(&lwin));

	/* Repeated :regular does nothing. */
	assert_success(cmds_dispatch("regular", &lwin, CIT_COMMAND));
	assert_false(flist_custom_active(&lwin));
}

TEST(plugin_command)
{
	curr_stats.vlua = vlua_init();
	curr_stats.plugs = plugs_create(curr_stats.vlua);

	ui_sb_msg("");
	assert_failure(cmds_dispatch("plugin load all", &lwin, CIT_COMMAND));
	assert_string_equal("Trailing characters", ui_sb_last());
	assert_failure(cmds_dispatch("plugin wrong arg", &lwin, CIT_COMMAND));
	assert_string_equal("Unknown subcommand: wrong", ui_sb_last());
	assert_failure(cmds_dispatch("plugin args-count", &lwin, CIT_COMMAND));
	assert_string_equal("Too few arguments", ui_sb_last());

	assert_success(cmds_dispatch("plugin load", &lwin, CIT_COMMAND));

	strlist_t empty_list = {};
	char *plug_items[] = { "plug" };
	strlist_t plug_list = { .items = plug_items, .nitems = 1 };

	ui_sb_msg("");
	assert_success(cmds_dispatch("plugin blacklist plug", &lwin, CIT_COMMAND));
	assert_string_equal("", ui_sb_last());

	strings_list_is(plug_list, plugs_get_blacklist(curr_stats.plugs));
	strings_list_is(empty_list, plugs_get_whitelist(curr_stats.plugs));

	ui_sb_msg("");
	assert_success(cmds_dispatch("plugin whitelist plug", &lwin, CIT_COMMAND));
	assert_success(cmds_dispatch("plugin whitelist plug", &lwin, CIT_COMMAND));
	assert_string_equal("", ui_sb_last());

	strings_list_is(plug_list, plugs_get_blacklist(curr_stats.plugs));
	strings_list_is(plug_list, plugs_get_whitelist(curr_stats.plugs));

	plugs_free(curr_stats.plugs);
	curr_stats.plugs = NULL;
	vlua_finish(curr_stats.vlua);
	curr_stats.vlua = NULL;
}

TEST(help_command)
{
	curr_stats.exec_env_type = EET_EMULATOR;
	update_string(&cfg.vi_command, "#vifmtest#editor");

	curr_stats.vlua = vlua_init();

	GLUA_EQ(curr_stats.vlua, "",
			"function handler(info) ginfo = info; return { success = false } end");
	GLUA_EQ(curr_stats.vlua, "",
			"vifm.addhandler{ name = 'editor', handler = handler }");

	cfg.use_vim_help = 0;

	assert_success(cmds_dispatch("help", &lwin, CIT_COMMAND));

	char help_file[PATH_MAX + 1];
	build_path(help_file, sizeof(help_file), get_installed_data_dir(),
			VIFM_HELP);

	GLUA_EQ(curr_stats.vlua, "edit-one", "print(ginfo.action)");
	GLUA_EQ(curr_stats.vlua, help_file, "print(ginfo.path)");
	GLUA_EQ(curr_stats.vlua, "false", "print(ginfo.mustwait)");
	GLUA_EQ(curr_stats.vlua, "nil", "print(ginfo.line)");
	GLUA_EQ(curr_stats.vlua, "nil", "print(ginfo.column)");

	cfg.use_vim_help = 1;

	assert_success(cmds_dispatch("help", &lwin, CIT_COMMAND));

	GLUA_EQ(curr_stats.vlua, "open-help", "print(ginfo.action)");
	GLUA_EQ(curr_stats.vlua, "vifm-app.txt", "print(ginfo.topic)");
	GLUA_ENDS(curr_stats.vlua, "/vim-doc", "print(ginfo.vimdocdir)");

	cfg.use_vim_help = 0;

	vlua_finish(curr_stats.vlua);
	curr_stats.vlua = NULL;
}

TEST(view_command)
{
	opt_handlers_setup();

	curr_stats.preview.on = 0;

	assert_success(cmds_dispatch("view", &lwin, CIT_COMMAND));
	assert_true(curr_stats.preview.on);

	assert_success(cmds_dispatch("view", &lwin, CIT_COMMAND));
	assert_false(curr_stats.preview.on);

	assert_success(cmds_dispatch("view!", &lwin, CIT_COMMAND));
	assert_true(curr_stats.preview.on);

	assert_success(cmds_dispatch("view!", &lwin, CIT_COMMAND));
	assert_true(curr_stats.preview.on);

	assert_success(cmds_dispatch("view", &lwin, CIT_COMMAND));
	assert_false(curr_stats.preview.on);

	opt_handlers_teardown();
}

TEST(invert_command)
{
	opt_handlers_setup();

	ui_sb_msg("");
	assert_failure(cmds_dispatch("set sort? sortorder?", &lwin, CIT_COMMAND));
	assert_string_equal("  sort=+name\n  sortorder=ascending", ui_sb_last());

	assert_success(cmds_dispatch("invert o", &lwin, CIT_COMMAND));

	ui_sb_msg("");
	assert_failure(cmds_dispatch("set sort? sortorder?", &lwin, CIT_COMMAND));
	assert_string_equal("  sort=-name\n  sortorder=descending", ui_sb_last());

	opt_handlers_teardown();
}

TEST(locate_command)
{
	ui_sb_msg("");

	/* Nothing to repeat. */
	assert_failure(cmds_dispatch("locate", &lwin, CIT_COMMAND));
	assert_string_equal("Nothing to repeat", ui_sb_last());
}

TEST(registers_command)
{
	regs_init();
	curr_stats.load_stage = -1;

	regs_append(DEFAULT_REG_NAME, "def");

	assert_success(cmds_dispatch1("registers", &lwin, CIT_COMMAND));
	assert_int_equal(2, menu_get_current()->len);

	regs_append('a', "a");
	regs_append('b', "b1");
	regs_append('b', "b2");

	assert_success(cmds_dispatch1("registers aababaa", &lwin, CIT_COMMAND));
	assert_int_equal(5, menu_get_current()->len);

	curr_stats.load_stage = 0;
	regs_reset();
}

TEST(open_command)
{
	curr_stats.vlua = vlua_init();

	GLUA_EQ(curr_stats.vlua, "",
			"function editor(i) info = i end");
	GLUA_EQ(curr_stats.vlua, "",
			"vifm.addhandler{ name = 'editor', handler = editor }");

	update_string(&cfg.vi_command, "#vifmtest#editor");

	create_file(SANDBOX_PATH "/to-open");
	create_file(SANDBOX_PATH "/to-open-2");

	lwin.list_rows = 2;
	lwin.list_pos = 0;
	lwin.dir_entry = dynarray_cextend(NULL,
			lwin.list_rows*sizeof(*lwin.dir_entry));
	lwin.dir_entry[0].name = strdup("to-open");
	lwin.dir_entry[0].origin = &lwin.curr_dir[0];
	lwin.dir_entry[1].name = strdup("to-open-2");
	lwin.dir_entry[1].origin = &lwin.curr_dir[0];
	strcpy(lwin.curr_dir, sandbox);

	assert_success(cmds_dispatch1("open \"comment", &lwin, CIT_COMMAND));
	GLUA_EQ(curr_stats.vlua, "1", "print(#info.paths)");
	GLUA_EQ(curr_stats.vlua, "to-open", "print(info.paths[1])");

	assert_success(cmds_dispatch1("%open", &lwin, CIT_COMMAND));
	GLUA_EQ(curr_stats.vlua, "2", "print(#info.paths)");
	GLUA_EQ(curr_stats.vlua, "to-open", "print(info.paths[1])");
	GLUA_EQ(curr_stats.vlua, "to-open-2", "print(info.paths[2])");

	remove_file(SANDBOX_PATH "/to-open");
	remove_file(SANDBOX_PATH "/to-open-2");

	vlua_finish(curr_stats.vlua);
	curr_stats.vlua = NULL;
}

TEST(mark_command)
{
	/* Bad mark name. */
	ui_sb_msg("");
	assert_failure(cmds_dispatch1("mark ab", &lwin, CIT_COMMAND));
	assert_string_equal("Invalid mark name: ab", ui_sb_last());
	assert_failure(cmds_dispatch1("mark &", &lwin, CIT_COMMAND));
	assert_string_equal("Invalid mark name: &", ui_sb_last());

	/* Relative paths are rejected. */
	ui_sb_msg("");
	assert_failure(cmds_dispatch1("mark x aaaaa", &lwin, CIT_COMMAND));
	assert_string_equal("Expected full path to a directory", ui_sb_last());

	/* Environment variables are expanded. */
	assert_success(cmds_dispatch1("let $TEST = '/'", &lwin, CIT_COMMAND));
	assert_success(cmds_dispatch1("mark x $TEST", &lwin, CIT_COMMAND));
	const mark_t *mark = get_mark_by_name(&lwin, 'x');
	assert_non_null(mark);
	assert_string_equal("/", mark->directory);

	/* Question mark prevents mark overwrite. */
	ui_sb_msg("");
	assert_failure(cmds_dispatch1("mark? x /tmp", &lwin, CIT_COMMAND));
	assert_string_equal("Mark isn't empty: x", ui_sb_last());
	/* Not an overwrite. */
	assert_success(cmds_dispatch1("mark? y /tmp", &lwin, CIT_COMMAND));
}

TEST(messages_command)
{
	assert_success(stats_init(&cfg));

	/* Nothing is printed when the history is empty. */
	ui_sb_msg("");
	assert_success(cmds_dispatch1("messages", &lwin, CIT_COMMAND));
	assert_string_equal("", ui_sb_last());

	/* An informational message is stored. */
	ui_sb_msg("1 info");
	assert_failure(cmds_dispatch1("messages", &lwin, CIT_COMMAND));
	assert_string_equal("1 info", ui_sb_last());

	/* An empty message isn't stored. */
	ui_sb_msg("");
	assert_failure(cmds_dispatch1("messages", &lwin, CIT_COMMAND));
	assert_string_equal("1 info", ui_sb_last());

	/* Error messages are stored as well.  All messages are appened together. */
	ui_sb_err("2 error");
	ui_sb_err("3 error");
	ui_sb_msg("4 info");
	assert_failure(cmds_dispatch1("messages", &lwin, CIT_COMMAND));
	assert_string_equal("1 info\n2 error\n3 error\n4 info", ui_sb_last());

	/* Output of the command is not stored in history. */
	assert_failure(cmds_dispatch1("messages", &lwin, CIT_COMMAND));
	assert_string_equal("1 info\n2 error\n3 error\n4 info", ui_sb_last());

	/* History is limited in its size. */
	unsigned int i;
	for(i = 0; i < ARRAY_LEN(curr_stats.msgs) - 4; ++i)
	{
		ui_sb_msgf("%d info", 4 + i);
	}
	assert_failure(cmds_dispatch1("messages", &lwin, CIT_COMMAND));
	assert_string_starts_with("1 info\n", ui_sb_last());
	assert_string_ends_with("\n50 info", ui_sb_last());

	/* History only keeps the most recent entries. */
	ui_sb_msg("51 info");
	assert_failure(cmds_dispatch1("messages", &lwin, CIT_COMMAND));
	assert_string_starts_with("2 error\n", ui_sb_last());
	assert_string_ends_with("\n51 info", ui_sb_last());

	/* History can be cleared. */
	ui_sb_msg("");
	assert_failure(cmds_dispatch1("messages typo", &lwin, CIT_COMMAND));
	assert_string_equal("Invalid argument: typo", ui_sb_last());
	ui_sb_msg("");
	assert_success(cmds_dispatch1("messages clear", &lwin, CIT_COMMAND));
	assert_string_equal("", ui_sb_last());
	assert_success(cmds_dispatch1("messages", &lwin, CIT_COMMAND));
	assert_string_equal("", ui_sb_last());
	/* And repopulated. */
	ui_sb_msg("new 1");
	ui_sb_msg("new 2");
	assert_failure(cmds_dispatch1("messages", &lwin, CIT_COMMAND));
	assert_string_equal("new 1\nnew 2", ui_sb_last());
}

static void
strings_list_is(const strlist_t expected, const strlist_t actual)
{
	assert_int_equal(expected.nitems, actual.nitems);

	int i;
	for(i = 0; i < MIN(expected.nitems, actual.nitems); ++i)
	{
		assert_string_equal(expected.items[i], actual.items[i]);
	}
}

/* vim: set tabstop=2 softtabstop=2 shiftwidth=2 noexpandtab cinoptions-=(0 : */
/* vim: set cinoptions+=t0 : */
