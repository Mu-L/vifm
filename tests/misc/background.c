#include <stic.h>

#include <unistd.h> /* chdir() usleep() */

#include <stdio.h> /* FILE fclose() fputs() */
#include <stdlib.h> /* free() */
#include <string.h> /* strdup() */

#include <test-utils.h>

#include "../../src/compat/pthread.h"
#include "../../src/compat/os.h"
#include "../../src/engine/var.h"
#include "../../src/engine/variables.h"
#include "../../src/utils/cancellation.h"
#include "../../src/utils/env.h"
#include "../../src/utils/string_array.h"
#include "../../src/ui/ui.h"
#include "../../src/background.h"
#include "../../src/signals.h"
#include "../../src/status.h"

static void on_job_exit(struct bg_job_t *job, void *data);
static void task(bg_op_t *bg_op, void *arg);
static void wait_until_locked(pthread_spinlock_t *lock);

SETUP_ONCE()
{
	setup_signals();
}

SETUP()
{
	/* curr_view shouldn't be NULL, because of iteration over tabs before doing
	 * exec(). */
	curr_view = &lwin;

	conf_setup();
}

TEARDOWN()
{
	conf_teardown();

	curr_view = NULL;
}

/* This test is the first one to make it pass faster.  When it's first, there
 * are no other jobs which can slow down receiving errors from the process. */
TEST(capture_error_of_external_command)
{
	bg_job_t *job = bg_run_external_job("echo there 1>&2", BJF_CAPTURE_OUT,
			/*descr=*/NULL, /*pwd=*/NULL);
	assert_non_null(job);
	assert_non_null(job->output);

	int nlines;
	char **lines = read_stream_lines(job->output, &nlines, 0, NULL, NULL);
	assert_int_equal(0, nlines);
	free_string_array(lines, nlines);

	while(1)
	{
		pthread_spin_lock(&job->errors_lock);
		if(job->errors == NULL)
		{
			pthread_spin_unlock(&job->errors_lock);
			usleep(5000);
			continue;
		}
		assert_string_starts_with("there", job->errors);
		pthread_spin_unlock(&job->errors_lock);
		break;
	}

	assert_success(bg_job_wait(job));
	assert_int_equal(0, job->exit_code);

	bg_job_decref(job);
}

TEST(provide_input_to_external_command_no_job, IF(have_cat))
{
	assert_success(chdir(SANDBOX_PATH));

	FILE *input;
	assert_success(bg_run_external("cat > file", /*keep_in_fg=*/0,
				/*skip_errors=*/1, SHELL_BY_USER, &input));
	assert_non_null(input);

	fputs("input", input);
	fclose(input);

	wait_for_all_bg();

	const char *lines[] = { "input" };
	file_is("file", lines, ARRAY_LEN(lines));

	remove_file("file");
}

TEST(jobcount_variable_gets_updated)
{
	(void)stats_update_fetch();

	var_t var = var_from_int(0);
	setvar("v:jobcount", var);
	var_free(var);

	pthread_spinlock_t locks[2];
	pthread_spin_init(&locks[0], PTHREAD_PROCESS_PRIVATE);
	pthread_spin_init(&locks[1], PTHREAD_PROCESS_PRIVATE);

	assert_int_equal(0, var_to_int(getvar("v:jobcount")));
	assert_false(stats_redraw_planned());

	assert_success(bg_execute("", "", 0, 0, &task, (void *)locks));

	wait_until_locked(&locks[0]);
	check_bg_jobs();

	assert_int_equal(1, var_to_int(getvar("v:jobcount")));
	assert_true(stats_redraw_planned());

	(void)stats_update_fetch();
	check_bg_jobs();

	assert_int_equal(1, var_to_int(getvar("v:jobcount")));
	assert_false(stats_redraw_planned());

	pthread_spin_lock(&locks[1]);
	pthread_spin_lock(&locks[0]);
	pthread_spin_unlock(&locks[0]);
	pthread_spin_unlock(&locks[1]);
	pthread_spin_destroy(&locks[0]);
	pthread_spin_destroy(&locks[1]);
}

TEST(job_can_survive_on_its_own)
{
	assert_success(bg_run_external("exit 71", /*keep_in_fg=*/0, /*skip_errors=*/1,
				SHELL_BY_APP, NULL));
	assert_int_equal(71, wait_for_job(bg_jobs));
}

TEST(explicitly_wait_for_a_job)
{
	assert_success(bg_run_external("exit 99", /*keep_in_fg=*/0, /*skip_errors=*/1,
				SHELL_BY_APP, NULL));

	bg_job_t *job = bg_jobs;
	assert_non_null(job);

	bg_job_incref(job);

	assert_success(bg_job_wait(job));
	assert_int_equal(99, job->exit_code);

	bg_job_decref(job);
}

TEST(create_a_job_explicitly)
{
	bg_job_t *job = bg_run_external_job("exit 5", BJF_CAPTURE_OUT, /*descr=*/NULL,
			/*pwd=*/NULL);
	assert_non_null(job);

	assert_success(bg_job_wait(job));
	assert_int_equal(5, job->exit_code);

	bg_job_decref(job);
}

TEST(capture_output_of_external_command)
{
	bg_job_t *job = bg_run_external_job("echo there", BJF_CAPTURE_OUT,
			/*descr=*/NULL, /*pwd=*/NULL);
	assert_non_null(job);
	assert_non_null(job->output);

	int nlines;
	char **lines = read_stream_lines(job->output, &nlines, 0, NULL, NULL);
	assert_int_equal(1, nlines);
	assert_string_equal("there", lines[0]);
	free_string_array(lines, nlines);

	assert_success(bg_job_wait(job));
	assert_int_equal(0, job->exit_code);

	bg_job_decref(job);
}

TEST(jobs_exit_cb_is_called)
{
	bg_job_t *job = bg_run_external_job("echo there", BJF_NONE, /*descr=*/NULL,
			/*pwd=*/NULL);
	assert_non_null(job);

	int called = 0;
	bg_job_set_exit_cb(job, &on_job_exit, &called);

	assert_int_equal(0, wait_for_job(job));
	bg_job_decref(job);

	assert_int_equal(1, called);
}

static void
on_job_exit(struct bg_job_t *job, void *data)
{
	int *called = data;
	*called = 1;
}

TEST(bgjob_good_pwd)
{
	assert_success(os_chdir(SANDBOX_PATH));
	create_dir("sub");
#ifndef _WIN32
	const char *cmd = "pwd";
#else
	const char *cmd = "echo %CD%";
#endif
	bg_job_t *job =
		bg_run_external_job(cmd, BJF_CAPTURE_OUT, /*descr=*/NULL, "sub");

	int nlines;
	char **lines = read_stream_lines(job->output, &nlines, 0, NULL, NULL);
	assert_int_equal(1, nlines);
	assert_string_contains("sub", lines[0]);
	free_string_array(lines, nlines);

	/* Removal might require the job to stop. */
	assert_success(bg_job_wait(job));
	bg_job_decref(job);

	remove_dir("sub");
}

TEST(bgjob_bad_pwd_causes_error)
{
	bg_job_t *job = bg_run_external_job("echo", BJF_CAPTURE_OUT, /*descr=*/NULL,
			"no-such-path");
	assert_null(job);
}

TEST(supply_input_to_external_command, IF(have_cat))
{
	bg_job_t *job = bg_run_external_job("cat", BJF_CAPTURE_OUT | BJF_SUPPLY_INPUT,
			/*descr=*/NULL, /*pwd=*/NULL);
	assert_non_null(job);
	assert_non_null(job->input);
	assert_non_null(job->output);

	fputs("1\n", job->input);
	fputs("2 2\n", job->input);
	fputs(" 3  3   3  ", job->input);
	fclose(job->input);
	job->input = NULL;

	int nlines;
	char **lines = read_stream_lines(job->output, &nlines, 0, NULL, NULL);
	assert_int_equal(3, nlines);
	assert_string_equal("1", lines[0]);
	assert_string_equal("2 2", lines[1]);
	assert_string_equal(" 3  3   3  ", lines[2]);
	free_string_array(lines, nlines);

	assert_success(bg_job_wait(job));
	assert_int_equal(0, job->exit_code);

	bg_job_decref(job);
}

TEST(background_redirects_streams_properly, IF(not_windows))
{
	assert_success(bg_and_wait_for_errors("echo a", &no_cancellation));
}

TEST(can_run_command_starting_with_a_dash, IF(not_windows))
{
	char sandbox[PATH_MAX + 1];
	make_abs_path(sandbox, sizeof(sandbox), SANDBOX_PATH, "", /*cwd=*/NULL);

	create_executable(SANDBOX_PATH "/-script");
	make_file(SANDBOX_PATH "/-script", "#!/bin/sh");

	char *saved_path_env = strdup(env_get("PATH"));
	env_set("PATH", sandbox);

	assert_success(bg_and_wait_for_errors("-script", &no_cancellation));

	env_set("PATH", saved_path_env);
	free(saved_path_env);

	remove_file(SANDBOX_PATH "/-script");
}

static void
task(bg_op_t *bg_op, void *arg)
{
	pthread_spinlock_t *locks = arg;
	pthread_spin_lock(&locks[0]);
	wait_until_locked(&locks[1]);
	pthread_spin_unlock(&locks[0]);
}

static void
wait_until_locked(pthread_spinlock_t *lock)
{
	while(pthread_spin_trylock(lock) == 0)
	{
		usleep(5000);
		pthread_spin_unlock(lock);
		usleep(5000);
	}
}

/* vim: set tabstop=2 softtabstop=2 shiftwidth=2 noexpandtab cinoptions-=(0 : */
/* vim: set cinoptions+=t0 filetype=c : */
