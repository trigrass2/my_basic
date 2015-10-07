/*
** This source file is part of MY-BASIC
**
** For the latest info, see https://github.com/paladin-t/my_basic/
**
** Copyright (C) 2011 - 2015 Wang Renxin
**
** Permission is hereby granted, free of charge, to any person obtaining a copy of
** this software and associated documentation files (the "Software"), to deal in
** the Software without restriction, including without limitation the rights to
** use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
** the Software, and to permit persons to whom the Software is furnished to do so,
** subject to the following conditions:
**
** The above copyright notice and this permission notice shall be included in all
** copies or substantial portions of the Software.
**
** THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
** IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
** FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
** COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
** IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
** CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#ifdef _MSC_VER
#	ifndef _CRT_SECURE_NO_WARNINGS
#		define _CRT_SECURE_NO_WARNINGS
#	endif /* _CRT_SECURE_NO_WARNINGS */
#endif /* _MSC_VER */

#include "../core/my_basic.h"
#ifdef _MSC_VER
#	include <crtdbg.h>
#	include <conio.h>
#elif !defined __BORLANDC__ && !defined __TINYC__
#	include <unistd.h>
#endif /* _MSC_VER */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#ifdef _MSC_VER
#	pragma warning(disable : 4127)
#	pragma warning(disable : 4706)
#	pragma warning(disable : 4996)
#endif /* _MSC_VER */

#ifdef __BORLANDC__
#	pragma warn -8004
#	pragma warn -8008
#	pragma warn -8066
#endif /* __BORLANDC__ */

#ifdef __POCC__
#	define unlink _unlink
#	define strdup _strdup
#endif /* __POCC__ */

/*
** {========================================================
** Common declarations
*/

#ifdef _MSC_VER
#	define _BIN_FILE_NAME "my_basic"
#elif defined __APPLE__
#	define _BIN_FILE_NAME "my_basic_mac"
#else /* _MSC_VER */
#	define _BIN_FILE_NAME "my_basic_bin"
#endif /* _MSC_VER */

#define _USE_MEM_POOL /* Comment this macro to disable memory pool */

#define _MAX_LINE_LENGTH 256
#define _str_eq(__str1, __str2) (_strcmpi(__str1, __str2) == 0)

#define _LINE_INC_STEP 16

#define _NO_END(s) (MB_FUNC_OK == s || MB_FUNC_SUSPEND == s || MB_FUNC_WARNING == s || MB_FUNC_ERR == s || MB_FUNC_END == s)

static struct mb_interpreter_t* bas = 0;

/* ========================================================} */

/*
** {========================================================
** Common
*/

#ifndef _printf
#	define _printf printf
#endif /* _printf */

/* ========================================================} */

/*
** {========================================================
** Memory manipulation
*/

#ifdef _USE_MEM_POOL
extern const size_t MB_SIZEOF_INT;
extern const size_t MB_SIZEOF_PTR;
extern const size_t MB_SIZEOF_LSN;
extern const size_t MB_SIZEOF_HTN;
extern const size_t MB_SIZEOF_OBJ;
extern const size_t MB_SIZEOF_FUN;
extern const size_t MB_SIZEOF_ARR;
extern const size_t MB_SIZEOF_VAR;
extern const size_t MB_SIZEOF_LBL;
extern const size_t MB_SIZEOF_RTN;
extern const size_t MB_SIZEOF_CLS;

typedef unsigned _pool_chunk_size_t;

typedef union _pool_tag_t {
	_pool_chunk_size_t size;
	void* ptr;
} _pool_tag_t;

typedef struct _pool_t {
	size_t size;
	char* stack;
} _pool_t;

static int pool_count = 0;

static _pool_t* pool = 0;

static long alloc_count = 0;

#define _POOL_NODE_ALLOC(size) (((char*)malloc(sizeof(_pool_tag_t) + size)) + sizeof(_pool_tag_t))
#define _POOL_NODE_PTR(s) (s - sizeof(_pool_tag_t))
#define _POOL_NODE_NEXT(s) (*((void**)(s - sizeof(_pool_tag_t))))
#define _POOL_NODE_SIZE(s) (*((_pool_chunk_size_t*)(s - sizeof(_pool_tag_t))))
#define _POOL_NODE_FREE(s) free(_POOL_NODE_PTR(s))

static void _open_mem_pool(void) {
#define N 11
	size_t szs[N];
	size_t lst[N];
	int i = 0;
	int j = 0;
	size_t s = 0;

	pool_count = 0;

	szs[0] = MB_SIZEOF_INT;
	szs[1] = MB_SIZEOF_PTR;
	szs[2] = MB_SIZEOF_LSN;
	szs[3] = MB_SIZEOF_HTN;
	szs[4] = MB_SIZEOF_OBJ;
	szs[5] = MB_SIZEOF_FUN;
	szs[6] = MB_SIZEOF_ARR;
	szs[7] = MB_SIZEOF_VAR;
	szs[8] = MB_SIZEOF_LBL;
	szs[9] = MB_SIZEOF_RTN;
	szs[10] = MB_SIZEOF_CLS;

	memset(lst, 0, sizeof(lst));

	/* Find all unduplicated sizes */
	for(i = 0; i < N; i++) {
		s = szs[i];
		for(j = 0; j < N; j++) {
			if(!lst[j]) {
				lst[j] = s;
				pool_count++;

				break;
			} else if(lst[j] == s) {
				break;
			}
		}
	}

	pool = (_pool_t*)malloc(sizeof(_pool_t) * pool_count);
	for(i = 0; i < pool_count; i++) {
		pool[i].size = lst[i];
		pool[i].stack = 0;
	}
#undef N
}

static void _close_mem_pool(void) {
	int i = 0;
	char* s = 0;

	if(!pool_count)
		return;

	for(i = 0; i < pool_count; i++) {
		while((s = pool[i].stack)) {
			pool[i].stack = _POOL_NODE_NEXT(s);
			_POOL_NODE_FREE(s);
		}
	}

	free((void*)pool);
	pool = 0;
	pool_count = 0;
}

static char* _pop_mem(unsigned s) {
	int i = 0;
	_pool_t* pl = 0;
	char* result = 0;

	if(pool_count) {
		for(i = 0; i < pool_count; i++) {
			pl = &pool[i];
			if(s == pl->size) {
				if(pl->stack) {
					/* Pop from stack */
					result = pl->stack;
					pl->stack = _POOL_NODE_NEXT(result);
					_POOL_NODE_SIZE(result) = (_pool_chunk_size_t)s;
					++alloc_count;

					return result;
				} else {
					/* Create a new node */
					result = _POOL_NODE_ALLOC(s);
					_POOL_NODE_SIZE(result) = (_pool_chunk_size_t)s;
					++alloc_count;

					return result;
				}
			}
		}
	}

	/* Allocate directly */
	result = _POOL_NODE_ALLOC(s);
	_POOL_NODE_SIZE(result) = (_pool_chunk_size_t)0;
	++alloc_count;

	return result;
}

static void _push_mem(char* p) {
	int i = 0;
	_pool_t* pl = 0;

	if(--alloc_count < 0) {
		mb_assert(0 && "Multiple free");
	}

	if(pool_count) {
		for(i = 0; i < pool_count; i++) {
			pl = &pool[i];
			if(_POOL_NODE_SIZE(p) == pl->size) {
				/* Push to stack */
				_POOL_NODE_NEXT(p) = pl->stack;
				pl->stack = p;

				return;
			}
		}
	}

	/* Free directly */
	_POOL_NODE_FREE(p);
}
#endif /* _USE_MEM_POOL */

/* ========================================================} */

/*
** {========================================================
** Code manipulation
*/

typedef struct _code_line_t {
	char** lines;
	int count;
	int size;
} _code_line_t;

static _code_line_t* c = 0;

static _code_line_t* _create_code(void) {
	_code_line_t* result = (_code_line_t*)malloc(sizeof(_code_line_t));
	result->count = 0;
	result->size = _LINE_INC_STEP;
	result->lines = (char**)malloc(sizeof(char*) * result->size);

	return result;
}

static void _destroy_code(_code_line_t* code) {
	int i = 0;

	mb_assert(code);

	for(i = 0; i < code->count; ++i) {
		free(code->lines[i]);
	}
	free(code->lines);
	free(code);
}

static void _clear_code(_code_line_t* code) {
	int i = 0;

	mb_assert(code);

	for(i = 0; i < code->count; ++i) {
		free(code->lines[i]);
	}
	code->count = 0;
}

static void _append_line(_code_line_t* code, char* txt) {
	int l = 0;
	char* buf = 0;

	mb_assert(code && txt);

	if(code->count + 1 == code->size) {
		code->size += _LINE_INC_STEP;
		code->lines = (char**)realloc(code->lines, sizeof(char*) * code->size);
	}
	l = (int)strlen(txt);
	buf = (char*)malloc(l + 2);
	memcpy(buf, txt, l);
	buf[l] = '\n';
	buf[l + 1] = '\0';
	code->lines[code->count++] = buf;
}

static char* _get_code(_code_line_t* code) {
	char* result = 0;
	int i = 0;

	mb_assert(code);

	result = (char*)malloc((_MAX_LINE_LENGTH + 2) * code->count + 1);
	result[0] = '\0';
	for(i = 0; i < code->count; ++i) {
		result = strcat(result, code->lines[i]);
		if(i != code->count - 1)
			result = strcat(result, "\n");
	}

	return result;
}

static void _set_code(_code_line_t* code, char* txt) {
	char* cursor = 0;
	char _c = '\0';

	mb_assert(code);

	if(!txt)
		return;

	_clear_code(code);
	cursor = txt;
	do {
		_c = *cursor;
		if(_c == '\r' || _c == '\n' || _c == '\0') {
			cursor[0] = '\0';
			if(_c == '\r' && *(cursor + 1) == '\n')
				++cursor;
			_append_line(code, txt);
			txt = cursor + 1;
		}
		++cursor;
	} while(_c);
}

static char* _load_file(const char* path) {
	FILE* fp = 0;
	char* result = 0;
	long curpos = 0;
	long l = 0;

	mb_assert(path);

	fp = fopen(path, "rb");
	if(fp) {
		curpos = ftell(fp);
		fseek(fp, 0L, SEEK_END);
		l = ftell(fp);
		fseek(fp, curpos, SEEK_SET);
		result = (char*)malloc((size_t)(l + 1));
		mb_assert(result);
		fread(result, 1, l, fp);
		fclose(fp);
		result[l] = '\0';
	}

	return result;
}

static int _save_file(const char* path, const char* txt) {
	FILE* fp = 0;

	mb_assert(path && txt);

	fp = fopen(path, "wb");
	if(fp) {
		fwrite(txt, sizeof(char), strlen(txt), fp);
		fclose(fp);

		return 1;
	}

	return 0;
}

/* ========================================================} */

/*
** {========================================================
** Interactive commands
*/

static void _clear_screen(void) {
#ifdef _MSC_VER
	system("cls");
#else /* _MSC_VER */
	system("clear");
#endif /* _MSC_VER */
}

static int _new_program(void) {
	_clear_code(c);

	return mb_reset(&bas, false);
}

static void _list_program(const char* sn, const char* cn) {
	long lsn = 0;
	long lcn = 0;

	mb_assert(sn && cn);

	lsn = atoi(sn);
	lcn = atoi(cn);
	if(lsn == 0 && lcn == 0) {
		long i = 0;
		for(i = 0; i < c->count; ++i) {
			_printf("%ld]%s", i + 1, c->lines[i]);
		}
	} else {
		long i = 0;
		long e = 0;
		if(lsn < 1 || lsn > c->count) {
			_printf("Line number %ld out of bound.\n", lsn);

			return;
		}
		if(lcn < 0) {
			_printf("Invalid line count %ld.\n", lcn);

			return;
		}
		--lsn;
		e = lcn ? lsn + lcn : c->count;
		for(i = lsn; i < e; ++i) {
			if(i >= c->count)
				break;

			_printf("%ld]%s\n", i + 1, c->lines[i]);
		}
	}
}

static void _edit_program(const char* no) {
	char line[_MAX_LINE_LENGTH];
	long lno = 0;
	int l = 0;

	mb_assert(no);

	lno = atoi(no);
	if(lno < 1 || lno > c->count) {
		_printf("Line number %ld out of bound.\n", lno);

		return;
	}
	--lno;
	memset(line, 0, _MAX_LINE_LENGTH);
	_printf("%ld]", lno + 1);
	mb_gets(line, _MAX_LINE_LENGTH);
	l = (int)strlen(line);
	c->lines[lno] = (char*)realloc(c->lines[lno], l + 2);
	strcpy(c->lines[lno], line);
	c->lines[lno][l] = '\n';
	c->lines[lno][l + 1] = '\0';
}

static void _insert_program(const char* no) {
	char line[_MAX_LINE_LENGTH];
	long lno = 0;
	int i = 0;

	mb_assert(no);

	lno = atoi(no);
	if(lno < 1 || lno > c->count) {
		_printf("Line number %ld out of bound.\n", lno);

		return;
	}
	--lno;
	memset(line, 0, _MAX_LINE_LENGTH);
	_printf("%ld]", lno + 1);
	mb_gets(line, _MAX_LINE_LENGTH);
	if(c->count + 1 == c->size) {
		c->size += _LINE_INC_STEP;
		c->lines = (char**)realloc(c->lines, sizeof(char*) * c->size);
	}
	for(i = c->count; i > lno; i--)
		c->lines[i] = c->lines[i - 1];
	c->lines[lno] = (char*)realloc(0, strlen(line) + 1);
	strcpy(c->lines[lno], line);
	c->count++;
}

static void _alter_program(const char* no) {
	long lno = 0;
	long i = 0;

	mb_assert(no);

	lno = atoi(no);
	if(lno < 1 || lno > c->count) {
		_printf("Line number %ld out of bound.\n", lno);

		return;
	}
	--lno;
	free(c->lines[lno]);
	for(i = lno; i < c->count - 1; i++)
		c->lines[i] = c->lines[i + 1];
	c->count--;
}

static void _load_program(const char* path) {
	char* txt = _load_file(path);
	if(txt) {
		_new_program();
		_set_code(c, txt);
		free(txt);
		if(c->count == 1) {
			_printf("Load done. %d line loaded.\n", c->count);
		} else {
			_printf("Load done. %d lines loaded.\n", c->count);
		}
	} else {
		_printf("Cannot load file \"%s\".\n", path);
	}
}

static void _save_program(const char* path) {
	char* txt = _get_code(c);
	if(!_save_file(path, txt)) {
		_printf("Cannot save file \"%s\".\n", path);
	} else {
		if(c->count == 1) {
			_printf("Save done. %d line saved.\n", c->count);
		} else {
			_printf("Save done. %d lines saved.\n", c->count);
		}
	}
	free(txt);
}

static void _kill_program(const char* path) {
	if(!unlink(path)) {
		_printf("Delete file \"%s\" successfully.\n", path);
	} else {
		_printf("Delete file \"%s\" failed.\n", path);
	}
}

static void _show_tip(void) {
	_printf("MY-BASIC Interpreter Shell - %s\n", mb_ver_string());
	_printf("Copyright (C) 2011 - 2015 Wang Renxin. All Rights Reserved.\n");
	_printf("For more information, see https://github.com/paladin-t/my_basic/.\n");
	_printf("Input HELP and hint enter to view help information.\n");
}

static void _show_help(void) {
	_printf("Parameters:\n");
	_printf("  %s           - Start interactive mode without arguments\n", _BIN_FILE_NAME);
	_printf("  %s *.*       - Load and run a file\n", _BIN_FILE_NAME);
	_printf("  %s -e \"expr\" - Evaluate an expression directly\n", _BIN_FILE_NAME);
	_printf("Interactive commands:\n");
	_printf("  CLS   - Clear screen\n");
	_printf("  NEW   - Clear current program\n");
	_printf("  RUN   - Run current program\n");
	_printf("  BYE   - Quit interpreter\n");
	_printf("  LIST  - List current program\n");
	_printf("          Usage: LIST [l [n]], l is start line number, n is line count\n");
	_printf("  EDIT  - Edit (modify/insert/remove) a line in current program\n");
	_printf("          Usage: EDIT n, n is line number\n");
	_printf("                 EDIT -I n, insert a line before a given line, n is line number\n");
	_printf("                 EDIT -R n, remove a line, n is line number\n");
	_printf("  LOAD  - Load a file as current program\n");
	_printf("          Usage: LOAD *.*\n");
	_printf("  SAVE  - Save current program to a file\n");
	_printf("          Usage: SAVE *.*\n");
	_printf("  KILL  - Delete a file\n");
	_printf("          Usage: KILL *.*\n");
}

static int _do_line(void) {
	int result = MB_FUNC_OK;
	char line[_MAX_LINE_LENGTH];
	char dup[_MAX_LINE_LENGTH];

	mb_assert(bas);

	memset(line, 0, _MAX_LINE_LENGTH);
	_printf("]");
	mb_gets(line, _MAX_LINE_LENGTH);

	memcpy(dup, line, _MAX_LINE_LENGTH);
	strtok(line, " ");

	if(_str_eq(line, "")) {
		/* Do nothing */
	} else if(_str_eq(line, "HELP")) {
		_show_help();
	} else if(_str_eq(line, "CLS")) {
		_clear_screen();
	} else if(_str_eq(line, "NEW")) {
		result = _new_program();
	} else if(_str_eq(line, "RUN")) {
		int i = 0;
		mb_assert(c);
		result = mb_reset(&bas, false);
		for(i = 0; i < c->count; ++i)
			mb_load_string(bas, c->lines[i]);
		result = mb_run(bas);
		_printf("\n");
	} else if(_str_eq(line, "BYE")) {
		result = MB_FUNC_BYE;
	} else if(_str_eq(line, "LIST")) {
		char* sn = line + strlen(line) + 1;
		char* cn = 0;
		strtok(sn, " ");
		cn = sn + strlen(sn) + 1;
		_list_program(sn, cn);
	} else if(_str_eq(line, "EDIT")) {
		char* no = line + strlen(line) + 1;
		char* ne = 0;
		strtok(no, " ");
		ne = no + strlen(no) + 1;
		if(!(*ne))
			_edit_program(no);
		else if(_str_eq(no, "-I"))
			_insert_program(ne);
		else if(_str_eq(no, "-R"))
			_alter_program(ne);
	} else if(_str_eq(line, "LOAD")) {
		char* path = line + strlen(line) + 1;
		_load_program(path);
	} else if(_str_eq(line, "SAVE")) {
		char* path = line + strlen(line) + 1;
		_save_program(path);
	} else if(_str_eq(line, "KILL")) {
		char* path = line + strlen(line) + 1;
		_kill_program(path);
	} else {
		_append_line(c, dup);
	}

	return result;
}

/* ========================================================} */

/*
** {========================================================
** Parameter processing
*/

#define _CHECK_ARG(__c, __i, __e) \
	do { \
		if(__c <= __i + 1) { \
			_printf(__e); \
			return; \
		} \
	} while(0)

static void _run_file(char* path) {
	if(mb_load_file(bas, path) == MB_FUNC_OK) {
		mb_run(bas);
	} else {
		_printf("Invalid file or error code.\n");
	}
}

static void _evaluate_expression(char* p) {
	char pr[8];
	int l = 0;
	int k = 0;
	bool_t a = true;
	char* e = 0;

	const char* const print = "PRINT ";

	if(!p) {
		_printf("Invalid expression.\n");

		return;
	}

	l = (int)strlen(p);
	k = (int)strlen(print);
	if(l >= k) {
		memcpy(pr, p, k);
		pr[k] = '\0';
		if(_str_eq(pr, print))
			a = false;
	}
	if(a) {
		e = (char*)malloc(l + k + 1);
		memcpy(e, print, k);
		memcpy(e + k, p, l);
		e[l + k] = '\0';
		p = e;
	}
	if(mb_load_string(bas, p) == MB_FUNC_OK) {
		mb_run(bas);
	} else {
		_printf("Invalid expression.\n");
	}
	if(a) {
		free(e);
	}
}

static void _process_parameters(int argc, char* argv[]) {
	int i = 1;
	char* p = 0;
	char m = '\0';

	while(i < argc) {
		if(!memcmp(argv[i], "-", 1)) {
			if(!memcmp(argv[i] + 1, "e", 1)) {
				m = 'e';
				_CHECK_ARG(argc, i, "-e: Expression expected.\n");
				p = argv[++i];
			} else {
				_printf("Unknown argument: %s.\n", argv[i]);
			}
		} else {
			p = argv[i];
		}

		i++;
	}

	switch(m) {
	case '\0':
		_run_file(p);
		break;
	case 'e':
		_evaluate_expression(p);
		break;
	}
}

/* ========================================================} */

/*
** {========================================================
** Scripting interfaces
*/

static int beep(struct mb_interpreter_t* s, void** l) {
	int result = MB_FUNC_OK;

	mb_assert(s && l);

	mb_check(mb_attempt_func_begin(s, l));
	mb_check(mb_attempt_func_end(s, l));

	putchar('\a');

	return result;
}

/* ========================================================} */

/*
** {========================================================
** Callbacks and handlers
*/

static void _on_stepped(struct mb_interpreter_t* s, int p, unsigned short row, unsigned short col) {
	mb_unrefvar(s);
	mb_unrefvar(p);
	mb_unrefvar(row);
	mb_unrefvar(col);
}

static void _on_error(struct mb_interpreter_t* s, mb_error_e e, char* m, char* f, int p, unsigned short row, unsigned short col, int abort_code) {
	mb_unrefvar(s);
	mb_unrefvar(f);
	mb_unrefvar(p);
	if(SE_NO_ERR != e) {
		_printf("Error:\n    [LINE] %d, [COL] %d,\n    [CODE] %d, [MESSAGE] %s, [ABORT CODE] %d.\n", row, col, e, m, abort_code);
	}
}

/* ========================================================} */

/*
** {========================================================
** Initialization and disposing
*/

static void _on_startup(void) {
#ifdef _USE_MEM_POOL
	_open_mem_pool();

	mb_set_memory_manager(_pop_mem, _push_mem);
#endif /* _USE_MEM_POOL */

	c = _create_code();

	mb_init();

	mb_open(&bas);
	mb_debug_set_stepped_handler(bas, _on_stepped);
	mb_set_error_handler(bas, _on_error);

	mb_reg_fun(bas, beep);
}

static void _on_exit(void) {
	mb_close(&bas);

	mb_dispose();

	_destroy_code(c);
	c = 0;

#ifdef _USE_MEM_POOL
	if(alloc_count > 0) {
		mb_assert(0 && "Memory leak");
	}
	_close_mem_pool();
#endif /* _USE_MEM_POOL */

#if defined _MSC_VER && !defined _WIN64
	if(0 != _CrtDumpMemoryLeaks()) { _asm { int 3 } }
#endif /* _MSC_VER && !_WIN64 */
}

/* ========================================================} */

/*
** {========================================================
** Entry
*/

int main(int argc, char* argv[]) {
	int status = 0;

#if defined _MSC_VER && !defined _WIN64
	_CrtSetBreakAlloc(0);
#endif /* _MSC_VER && !_WIN64 */

	atexit(_on_exit);

	_on_startup();

	if(argc == 1) {
		_show_tip();
		do {
			status = _do_line();
		} while(_NO_END(status));
	} else if(argc >= 2) {
		_process_parameters(argc, argv);
	}

	return 0;
}

/* ========================================================} */
