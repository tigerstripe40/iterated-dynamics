/* Parser.cpp (C) 1990, Mark C. Peterson, CompuServe [70441, 3353]
	All rights reserved.

	Code may be used in any program provided the author is credited
	either during program execution or in the documentation.  Source
	code may be distributed only in combination with public domain or
	shareware source code.  Source code may be modified provided the
	copyright notice and this message is left unchanged and all
	modifications are clearly documented.

	I would appreciate a copy of any work which incorporates this code,
	however this is optional.

	Mark C. Peterson
	405-C Queen St. Suite #181
	Southington, CT 06489
	(203) 276-9721
*/
#include <algorithm>
#include <cassert>
#include <ctime>
#include <sstream>
#include <string>

#include <boost/format.hpp>

#include "port.h"
#include "prototyp.h"
#include "helpdefs.h"

#include "drivers.h"
#include "Externals.h"
#include "filesystem.h"
#include "fpu.h"
#include "fractals.h"
#include "idhelp.h"
#include "jiim.h"
#include "miscres.h"
#include "mpmath.h"
#include "parser.h"
#include "prompts2.h"
#include "StopMessage.h"

#include "Formula.h"
#include "MathUtil.h"

#ifdef WATCH_MP
double x1;
double y1;
double x2;
double y2;
#endif

enum
{
	MAX_OPS = 250,
	MAX_ARGS = 100,
	MAX_BOXX = 8192  // max size of g_box_x array
};

static const int MAX_TOKEN_LENGTH = 32;
static const int CTRL_Z = 26;
static const int BITS_PER_BYTE = 8;

// token IDs
enum TokenIdType
{
	TOKENID_ERROR_END_OF_FILE = 1,
	TOKENID_ERROR_ILLEGAL_CHARACTER = 2,
	TOKENID_ERROR_ILLEGAL_VARIABLE_NAME = 3,
	TOKENID_ERROR_TOKEN_TOO_LONG = 4,
	TOKENID_ERROR_FUNC_USED_AS_VAR = 5,
	TOKENID_ERROR_JUMP_MISSING_BOOLEAN = 6,
	TOKENID_ERROR_JUMP_WITH_ILLEGAL_CHAR = 7,
	TOKENID_ERROR_UNDEFINED_FUNCTION = 8,
	TOKENID_ERROR_ILLEGAL_OPERATOR = 9,
	TOKENID_ERROR_ILL_FORMED_CONSTANT = 10,
	TOKENID_OPEN_PARENS = 1,
	TOKENID_CLOSE_PARENS = -1
};

struct PEND_OP
{
	void (*function)();
	int prec;
};

struct FormulaToken
{
	char text[MAX_TOKEN_LENGTH+1];
	FormulaTokenType type;
	int id;
	ComplexD value;

	void SetError(TokenIdType tokenId)
	{
		type = TOKENTYPE_ERROR;
		id = int(tokenId);
	}
	bool IsError(TokenIdType tokenId) const
	{
		return (type == TOKENTYPE_ERROR) && (id == int(tokenId));
	}
	void SetValue(double real)
	{
		value.real(real);
		value.imag(0.0);
		type = TOKENTYPE_REAL_CONSTANT;
	}
	void SetValue(double real, double imaginary)
	{
		value.real(real);
		value.imag(imaginary);
		type = TOKENTYPE_COMPLEX_CONSTANT;
	}
};

// indices correspond to VariableNames enum
struct constant_list_item
{
	const char *name;
	VariableNames variable;
	FormulaTokenType tokenType;
};
static constant_list_item s_constants[] =
{
	{ "pixel",		VARIABLE_PIXEL,		TOKENTYPE_PREDEFINED_VARIABLE },
	{ "p1",			VARIABLE_P1,		TOKENTYPE_PARAMETER_VARIABLE },
	{ "p2",			VARIABLE_P2,		TOKENTYPE_PARAMETER_VARIABLE },
	{ "z",			VARIABLE_Z,			TOKENTYPE_PREDEFINED_VARIABLE },
	{ "LastSqr",	VARIABLE_LAST_SQR,	TOKENTYPE_PREDEFINED_VARIABLE },
	{ "pi",			VARIABLE_PI,		TOKENTYPE_PREDEFINED_VARIABLE },
	{ "e",			VARIABLE_E,			TOKENTYPE_PREDEFINED_VARIABLE },
	{ "rand",		VARIABLE_RAND,		TOKENTYPE_PREDEFINED_VARIABLE },
	{ "p3",			VARIABLE_P3,		TOKENTYPE_PARAMETER_VARIABLE },
	{ "whitesq",	VARIABLE_WHITE_SQ,	TOKENTYPE_PREDEFINED_VARIABLE },
	{ "scrnpix",	VARIABLE_SCRN_PIX,	TOKENTYPE_PREDEFINED_VARIABLE },
	{ "scrnmax",	VARIABLE_SCRN_MAX,	TOKENTYPE_PREDEFINED_VARIABLE },
	{ "maxit",		VARIABLE_MAX_IT,	TOKENTYPE_PREDEFINED_VARIABLE },
	{ "ismand",		VARIABLE_IS_MAND,	TOKENTYPE_PREDEFINED_VARIABLE },
	{ "center",		VARIABLE_CENTER,	TOKENTYPE_PREDEFINED_VARIABLE },
	{ "magxmag",	VARIABLE_MAG_X_MAG,	TOKENTYPE_PREDEFINED_VARIABLE },
	{ "rotskew",	VARIABLE_ROT_SKEW,	TOKENTYPE_PREDEFINED_VARIABLE },
	{ "p4",			VARIABLE_P4,		TOKENTYPE_PARAMETER_VARIABLE },
	{ "p5",			VARIABLE_P5,		TOKENTYPE_PARAMETER_VARIABLE }
};

struct SYMMETRY
{
	const char *symmetry;
	SymmetryType n;
};

static SYMMETRY s_symmetry_list[] =
{
	{ "XAXIS_NOIMAG",	SYMMETRY_X_AXIS_NO_IMAGINARY },
	{ "PI_SYM_NOPARM",	SYMMETRY_PI_NO_PARAMETER },
	{ "ORIGIN_NOPARM",	SYMMETRY_ORIGIN_NO_PARAMETER },
	{ "XYAXIS_NOPARM",	SYMMETRY_XY_AXIS_NO_PARAMETER },
	{ "YAXIS_NOPARM",	SYMMETRY_Y_AXIS_NO_PARAMETER },
	{ "XAXIS_NOPARM",	SYMMETRY_X_AXIS_NO_PARAMETER },
	{ "NOSYM",			SYMMETRY_NONE },
	{ "XAXIS",			SYMMETRY_X_AXIS },
	{ "YAXIS",			SYMMETRY_Y_AXIS },
	{ "XYAXIS",			SYMMETRY_XY_AXIS },
	{ "ORIGIN",			SYMMETRY_ORIGIN },
	{ "PI_SYM",			SYMMETRY_PI },
	{ "XAXIS_NOREAL",	SYMMETRY_X_AXIS_NO_REAL },
	{ "NOPLOT",			SYMMETRY_NO_PLOT },
	{ "",				SYMMETRY_NONE }
};

class Random
{
public:
	Random()
		: m_random_number(0),
		m_set_random(false),
		m_randomized(false)
	{
	}
	~Random()
	{
	}

	bool random() const				{ return m_set_random; }
	bool randomized() const			{ return m_randomized; }

	void set_randomized(bool value) { m_randomized = value; }
	void set_random(bool value)		{ m_set_random = value; }

	unsigned long new_random_number();
	void set_random_function();
	void seed();

private:
	int m_random_number;
	bool m_set_random;
	bool m_randomized;
};

Formula g_formula_state;
double g_fudge_limit = 0.0;
bool g_is_mandelbrot = true;

static Random s_random;
static double s_fudge = 0.0;
static int s_delta16 = 0;
static int s_shift_back = 0;

Formula::Formula()
	: m_math_type(FLOATING_POINT_MATH),
	m_number_of_ops(0),
	m_number_of_loads(0),
	m_number_of_stores(0),
	m_number_of_jumps(0),
	m_initial_jump_index(0),
	m_variable_count(0),
	m_complex_count(0),
	m_real_count(0),
	m_chars_in_formula(0),
	m_next_operation(0),
	m_initial_n(0),
	m_parenthesis_count(0),
	m_expecting_arg(false),
	m_set_random(0),
	m_variable_list(),
	m_complex_list(),
	m_real_list(),
	m_last_op(0),
	m_parser_vsp(0),
	m_formula_max_ops(MAX_OPS),
	m_formula_max_args(MAX_ARGS),
	m_op_index(0),
	m_uses_jump(false),
	m_jump_index(0),
	m_store_ptr(0),
	m_load_ptr(0),
	m_is_mand(1),
	m_posp(0),
	m_last_init_op(0),
	m_uses_is_mand(0),
	m_fudge_limit(0.0),
	m_uses_p1(false),
	m_uses_p2(false),
	m_uses_p3(false),
	m_uses_p4(false),
	m_uses_p5(false),
	m_max_function_number(0),
	m_function_load_store_pointers(0),
	m_variables(0),
	m_store(0),
	m_load(0),
	m_functions(0),
	m_arg1(),
	m_arg2(),
	m_formula_text(0),
	m_file_pos(0),
	m_statement_pos(0),
	m_errors_found(0),
	m_initial_load_pointer(0),
	m_initial_store_pointer(0),
	m_initial_op_pointer(0)
{
	{
		Arg zero_arg = { { 0, 0 } };
		for (int i = 0; i < NUM_OF(m_argument_stack); i++)
		{
			m_argument_stack[i] = zero_arg;
		}
	}
	{
		JUMP_CONTROL zero_jump_control = { JUMPTYPE_NONE };
		for (int i = 0; i < NUM_OF(m_jump_control); i++)
		{
			m_jump_control[i] = zero_jump_control;
		}
	}
	{
		error_data zero_error_data = { 0 };
		for (int i = 0; i < NUM_OF(m_errors); i++)
		{
			m_errors[i] = zero_error_data;
		}
	}
	m_prepare_formula_text[0] = 0;
	m_filename[0] = 0;
	m_formula_name[0] = 0;
}

Formula::~Formula()
{
	free_work_area();
}

void Formula::set_filename(const char *value)
{
	m_filename = value;
}

void Formula::set_formula(const char *value)
{
	m_formula_name = value ? value : "";
}

bool Formula::merge_formula_filename(char *new_filename, int mode)
{
	std::string arg = new_filename;
	return ::merge_path_names(m_filename, arg, mode) < 0;
}

bool Formula::find_item(std::ifstream &file)
{
	return ::find_file_item(m_filename, m_formula_name, file, ITEMTYPE_FORMULA);
}

long Formula::get_file_entry(char *wildcard)
{
	return get_file_entry_help(FIHELP_FORMULA, GETFILE_FORMULA,
		"Formula", wildcard, m_filename, m_formula_name);
}

#define CASE_TERMINATOR case',':\
						case '\n':\
						case '(':\
						case ')':\
						case '!':\
						case '=':\
						case '<':\
						case '>':\
						case '|':\
						case '&':\
						case '}':\
						case ':':\
						case '+':\
						case '-':\
						case '*':\
						case '/':\
						case '^'

#define CASE_ALPHA      case 'a':\
						case 'b':\
						case 'c':\
						case 'd':\
						case 'e':\
						case 'f':\
						case 'g':\
						case 'h':\
						case 'i':\
						case 'j':\
						case 'k':\
						case 'l':\
						case 'm':\
						case 'n':\
						case 'o':\
						case 'p':\
						case 'q':\
						case 'r':\
						case 's':\
						case 't':\
						case 'u':\
						case 'v':\
						case 'w':\
						case 'x':\
						case 'y':\
						case 'z'

#define CASE_NUM        case '0':\
						case '1':\
						case '2':\
						case '3':\
						case '4':\
						case '5':\
						case '6':\
						case '7':\
						case '8':\
						case '9'

// CAE fp added MAX_STORES and LOADS
// MAX_STORES must be even to make Unix alignment work
// TW made dependent on m_formula_max_ops

#define MAX_STORES ((m_formula_max_ops/4)*2)  // at most only half the ops can be stores
#define MAX_LOADS (unsigned(m_formula_max_ops*.8))  // and 80% can be loads

static PEND_OP s_ops[2300];

Arg *g_argument1;
Arg *g_argument2;

#if !defined(NO_FIXED_POINT_MATH)
#define ChkLongDenom(denom)										\
	do															\
	{															\
		if (denom == 0 || g_overflow)							\
		{														\
			g_overflow = true;									\
			return;												\
		}														\
		else if (denom == 0)									\
		{														\
			return;												\
		}														\
	}															\
	while (0)

#endif

#define ChkFloatDenom(denom)			\
	do									\
	{									\
		if (std::abs(denom) <= DBL_MIN)		\
		{								\
			g_overflow = true;			\
			return;						\
		}								\
	}									\
	while (0)

/* error_messages() defines; all calls to error_messages(), or any variable which will
	be used as the argument in a call to error_messages(), should use one of these
	defines.
*/

enum ParserErrorType
{
	PE_NO_ERRORS_FOUND = -1,
	PE_SHOULD_BE_ARGUMENT,
	PE_SHOULD_BE_OPERATOR,
	PE_NEED_A_MATCHING_OPEN_PARENS,
	PE_NEED_MORE_CLOSE_PARENS,
	PE_UNDEFINED_OPERATOR,
	PE_UNDEFINED_FUNCTION,
	PE_TABLE_OVERFLOW,
	PE_NO_MATCH_RIGHT_PAREN,
	PE_NO_LEFT_BRACKET_FIRST_LINE,
	PE_UNEXPECTED_EOF,
	PE_INVALID_SYM_USING_NOSYM,
	PE_FORMULA_TOO_LARGE,
	PE_INSUFFICIENT_MEM_FOR_TYPE_FORMULA,
	PE_COULD_NOT_OPEN_FILE_WHERE_FORMULA_LOCATED,
	PE_JUMP_NOT_FIRST,
	PE_NO_CHAR_AFTER_THIS_JUMP,
	PE_JUMP_NEEDS_BOOLEAN,
	PE_ENDIF_REQUIRED_AFTER_ELSE,
	PE_ENDIF_WITH_NO_IF,
	PE_MISPLACED_ELSE_OR_ELSEIF,
	PE_UNMATCHED_IF_IN_INIT_SECTION,
	PE_IF_WITH_NO_ENDIF,
	PE_ERROR_IN_PARSING_JUMP_STATEMENTS,
	PE_TOO_MANY_JUMPS,
	PE_FORMULA_NAME_TOO_LARGE,
	PE_ILLEGAL_ASSIGNMENT,
	PE_ILLEGAL_VAR_NAME,
	PE_INVALID_CONST,
	PE_ILLEGAL_CHAR,
	PE_NESTING_TOO_DEEP,
	PE_UNMATCHED_MODULUS,
	PE_FUNC_USED_AS_VAR,
	PE_NO_NEG_AFTER_EXPONENT,
	PE_TOKEN_TOO_LONG,
	PE_SECOND_COLON,
	PE_INVALID_CALL_TO_PARSEERRS
};

const char *Formula::error_messages(int which)
{
	static const char *error_strings[] =
	{
		"Should be an Argument",
		"Should be an Operator",
		"')' needs a matching '('",
		"Need more ')'",
		"Undefined Operator",
		"Undefined Function",
		"Table overflow",
		"Didn't find matching ')' in symmetry declaration",
		"No '{' found on first line",
		"Unexpected EOF!",
		"Symmetry below is invalid, will use NOSYM",
		"Formula is too large",
		"Insufficient memory to run fractal type 'formula'",
		"Could not open file where formula located",
		"No characters may precede jump instruction",
		"No characters may follow this jump instruction",
		"Jump instruction missing required (boolean argument)",
		"Next jump after \"else\" must be \"endif\"",
		"\"endif\" has no matching \"if\"",
		"Misplaced \"else\" or \"elseif()\"",
		"\"if ()\" in initialization has no matching \"endif\"",
		"\"if ()\" has no matching \"endif\"",
		"Error in parsing jump statements",
		"Formula has too many jump commands",
		"Formula name has too many characters",
		"Only variables are allowed to left of assignment",
		"Illegal variable name",
		"Invalid constant expression",
		"This character not supported by parser",
		"Nesting of parentheses exceeds maximum depth",
		"Unmatched modulus operator \"|\" in this expression",
		"Can't use function name as variable",
		"Negative exponent must be enclosed in parens",
		"Variable or constant exceeds 32 character limit",
		"Only one \":\" permitted in a formula",
		"Invalid Formula::errors code"
	};
	int last_error = NUM_OF(error_strings) - 1;
	return error_strings[which > last_error ? last_error : which];
}

static int int_strlen(const char *text)
{
	return int(strlen(text));
}

/* use the following when only float functions are implemented to
	get MP math and Integer math */

#if !defined(NO_FIXED_POINT_MATH)
static void lStkFunct(void (*function)())   // call lStk via dStk
{
	/*
		intermediate variable needed for safety because of
		different size of double and long in Arg union
	*/
	ComplexD arg = ComplexFudgeToDouble(g_argument1->l);
	double y = FudgeToDouble(g_argument1->l.imag());
	g_argument1->d.real(FudgeToDouble(g_argument1->l.real()));
	g_argument1->d.imag(y);
	(*function)();
	if (std::abs(g_argument1->d.real()) < g_fudge_limit && std::abs(g_argument1->d.imag()) < g_fudge_limit)
	{
		g_argument1->l.real(DoubleToFudge(g_argument1->d.real()));
		g_argument1->l.imag(DoubleToFudge(g_argument1->d.imag()));
	}
	else
	{
		g_overflow = true;
	}
}
#endif

unsigned long Random::new_random_number()
{
	m_random_number = ((m_random_number << 15) + rand15()) ^ m_random_number;
	return m_random_number;
}

unsigned long new_random_number()
{
	return s_random.new_random_number();
}

void lRandom()
{
	g_formula_state.Random_l();
}

static long fixpoint_to_long(long quantity)
{
	return quantity >> (32 - g_bit_shift);
}

void Formula::Random_l()
{
	m_variables[VARIABLE_RAND].argument.l.real(fixpoint_to_long(new_random_number()));
	m_variables[VARIABLE_RAND].argument.l.imag(fixpoint_to_long(new_random_number()));
}

void dRandom()
{
	g_formula_state.Random_d();
}

void Formula::Random_d()
{
	/* Use the same algorithm as for fixed math so that they will generate
          the same fractals when the srand() function is used. */
	long x = fixpoint_to_long(new_random_number());
	long y = fixpoint_to_long(new_random_number());
	m_variables[VARIABLE_RAND].argument.d.real((double(x)/(1L << g_bit_shift)));
	m_variables[VARIABLE_RAND].argument.d.imag((double(y)/(1L << g_bit_shift)));

}

void Random::set_random_function()
{
	unsigned Seed;

	if (!m_set_random)
	{
		m_random_number = g_argument1->l.real() ^ g_argument1->l.imag();
	}

	Seed = unsigned(m_random_number)^unsigned(m_random_number >> 16);
	srand(Seed);
	m_set_random = true;

	// Clear out the seed
	new_random_number();
	new_random_number();
	new_random_number();
}

void Random::seed()
{
	time_t ltime;

	// Use the current time to randomize the random number sequence.
	time(&ltime);
	srand(unsigned(ltime));

	new_random_number();
	new_random_number();
	new_random_number();
	m_randomized = true;
}

void SetRandFnct()
{
	s_random.set_random_function();
}

void RandomSeed()
{
	s_random.seed();
}

void lStkSRand()
{
	g_formula_state.StackStoreRandom_l();
}

void Formula::StackStoreRandom_l()
{
#if !defined(NO_FIXED_POINT_MATH)
	SetRandFnct();
	lRandom();
	g_argument1->l = m_variables[VARIABLE_RAND].argument.l;
#endif
}

void dStkSRand()
{
	g_formula_state.StackStoreRandom_d();
}

void Formula::StackStoreRandom_d()
{
	g_argument1->l.real(long(g_argument1->d.real()*(1L << g_bit_shift)));
	g_argument1->l.imag(long(g_argument1->d.imag()*(1L << g_bit_shift)));
	SetRandFnct();
	dRandom();
	g_argument1->d = m_variables[VARIABLE_RAND].argument.d;
}

void (*StkSRand)() = dStkSRand;


void Formula::StackLoadDup_d()
{
	g_argument1 += 2;
	g_argument2 += 2;
	*g_argument2 = *g_argument1 = *m_load[m_load_ptr];
	m_load_ptr += 2;
}

void dStkLodSqr()
{
	return g_formula_state.StackLoadSqr_d();
}

void Formula::StackLoadSqr_d()
{
	g_argument1++;
	g_argument2++;
	g_argument1->d.imag(m_load[m_load_ptr]->d.real()*m_load[m_load_ptr]->d.imag()*2.0);
	g_argument1->d.real(sqr(m_load[m_load_ptr]->d.real()) - sqr(m_load[m_load_ptr]->d.imag()));
	m_load_ptr++;
}

void dStkLodSqr2()
{
	g_formula_state.StackLoadSqr2_d();
}

void Formula::StackLoadSqr2_d()
{
	g_argument1++;
	g_argument2++;
	m_variables[VARIABLE_LAST_SQR].argument.d.real(sqr(m_load[m_load_ptr]->d.real()));
	m_variables[VARIABLE_LAST_SQR].argument.d.imag(sqr(m_load[m_load_ptr]->d.imag()));
	g_argument1->d.imag(m_load[m_load_ptr]->d.real()*m_load[m_load_ptr]->d.imag()*2.0);
	g_argument1->d.real(m_variables[VARIABLE_LAST_SQR].argument.d.real() - m_variables[VARIABLE_LAST_SQR].argument.d.imag());
	m_variables[VARIABLE_LAST_SQR].argument.d.real(m_variables[VARIABLE_LAST_SQR].argument.d.real() + m_variables[VARIABLE_LAST_SQR].argument.d.imag());
	m_variables[VARIABLE_LAST_SQR].argument.d.imag(0);
	m_load_ptr++;
}

void dStkLodDbl()
{
	g_formula_state.StackLoadDouble();
}

void Formula::StackLoadDouble()
{
	g_argument1++;
	g_argument2++;
	g_argument1->d.real(m_load[m_load_ptr]->d.real()*2.0);
	g_argument1->d.imag(m_load[m_load_ptr]->d.imag()*2.0);
	m_load_ptr++;
}

void dStkSqr0()
{
	g_formula_state.StackSqr0();
}

void Formula::StackSqr0()
{
	m_variables[VARIABLE_LAST_SQR].argument.d.imag(sqr(g_argument1->d.imag())); // use LastSqr as temp storage
	g_argument1->d.imag(g_argument1->d.real()*g_argument1->d.imag()*2.0);
	g_argument1->d.real(sqr(g_argument1->d.real()) - m_variables[VARIABLE_LAST_SQR].argument.d.imag());
}

void dStkSqr3()
{
	g_argument1->d.real(sqr(g_argument1->d.real()));
}

void dStkAbs()
{
	g_argument1->d.real(std::abs(g_argument1->d.real()));
	g_argument1->d.imag(std::abs(g_argument1->d.imag()));
}

#if !defined(NO_FIXED_POINT_MATH)
void lStkAbs()
{
	g_argument1->l.real(labs(g_argument1->l.real()));
	g_argument1->l.imag(labs(g_argument1->l.imag()));
}
#endif

void (*StkAbs)() = dStkAbs;

void dStkSqr()
{
	g_formula_state.StackSqr_d();
}

void Formula::StackSqr_d()
{
	m_variables[VARIABLE_LAST_SQR].argument.d.real(sqr(g_argument1->d.real()));
	m_variables[VARIABLE_LAST_SQR].argument.d.imag(sqr(g_argument1->d.imag()));
	g_argument1->d.imag(g_argument1->d.real()*g_argument1->d.imag()*2.0);
	g_argument1->d.real(m_variables[VARIABLE_LAST_SQR].argument.d.real() - m_variables[VARIABLE_LAST_SQR].argument.d.imag());
	m_variables[VARIABLE_LAST_SQR].argument.d.real(m_variables[VARIABLE_LAST_SQR].argument.d.real() + m_variables[VARIABLE_LAST_SQR].argument.d.imag());
	m_variables[VARIABLE_LAST_SQR].argument.d.imag(0);
}

void lStkSqr()
{
	g_formula_state.StackSqr_l();
}

void Formula::StackSqr_l()
{
#if !defined(NO_FIXED_POINT_MATH)
	m_variables[VARIABLE_LAST_SQR].argument.l.real(multiply(g_argument1->l.real(), g_argument1->l.real(), g_bit_shift));
	m_variables[VARIABLE_LAST_SQR].argument.l.imag(multiply(g_argument1->l.imag(), g_argument1->l.imag(), g_bit_shift));
	g_argument1->l.imag(multiply(g_argument1->l.real(), g_argument1->l.imag(), g_bit_shift) << 1);
	g_argument1->l.real(m_variables[VARIABLE_LAST_SQR].argument.l.real() - m_variables[VARIABLE_LAST_SQR].argument.l.imag());
	m_variables[VARIABLE_LAST_SQR].argument.l.real(m_variables[VARIABLE_LAST_SQR].argument.l.real() + m_variables[VARIABLE_LAST_SQR].argument.l.imag());
	m_variables[VARIABLE_LAST_SQR].argument.l.imag(0L);
#endif
}

void (*StkSqr)() = dStkSqr;

void dStkAdd()
{
	g_argument2->d.real(g_argument2->d.real() + g_argument1->d.real());
	g_argument2->d.imag(g_argument2->d.imag() + g_argument1->d.imag());
	g_argument1--;
	g_argument2--;
}

#if !defined(NO_FIXED_POINT_MATH)
void lStkAdd()
{
	g_argument2->l.real(g_argument2->l.real() + g_argument1->l.real());
	g_argument2->l.imag(g_argument2->l.imag() + g_argument1->l.imag());
	g_argument1--;
	g_argument2--;
}
#endif

void (*StkAdd)() = dStkAdd;

void dStkSub()
{
	g_argument2->d.real(g_argument2->d.real() - g_argument1->d.real());
	g_argument2->d.imag(g_argument2->d.imag() - g_argument1->d.imag());
	g_argument1--;
	g_argument2--;
}

#if !defined(NO_FIXED_POINT_MATH)
void lStkSub()
{
	g_argument2->l.real(g_argument2->l.real() - g_argument1->l.real());
	g_argument2->l.imag(g_argument2->l.imag() - g_argument1->l.imag());
	g_argument1--;
	g_argument2--;
}
#endif

void (*StkSub)() = dStkSub;

void dStkConj()
{
	g_argument1->d = conj(g_argument1->d);
}

#if !defined(NO_FIXED_POINT_MATH)
void lStkConj()
{
	g_argument1->l = conj(g_argument1->l);
}
#endif

void (*StkConj)() = dStkConj;

void dStkFloor()
{
	g_argument1->d.real(floor(g_argument1->d.real()));
	g_argument1->d.imag(floor(g_argument1->d.imag()));
}

#if !defined(NO_FIXED_POINT_MATH)
void lStkFloor()
{
	/*
	* Kill fractional part. This operation truncates negative numbers
	* toward negative infinity as desired.
	*/
	g_argument1->l.real((g_argument1->l.real()) >> g_bit_shift);
	g_argument1->l.imag((g_argument1->l.imag()) >> g_bit_shift);
	g_argument1->l.real((g_argument1->l.real()) << g_bit_shift);
	g_argument1->l.imag((g_argument1->l.imag()) << g_bit_shift);
}
#endif

void (*StkFloor)() = dStkFloor;

void dStkCeil()
{
	g_argument1->d.real(ceil(g_argument1->d.real()));
	g_argument1->d.imag(ceil(g_argument1->d.imag()));
}

#if !defined(NO_FIXED_POINT_MATH)
void lStkCeil()
{
	/* the shift operation does the "floor" operation, so we
		negate everything before the operation */
	g_argument1->l.real((-g_argument1->l.real()) >> g_bit_shift);
	g_argument1->l.imag((-g_argument1->l.imag()) >> g_bit_shift);
	g_argument1->l.real(-((g_argument1->l.real()) << g_bit_shift));
	g_argument1->l.imag(-((g_argument1->l.imag()) << g_bit_shift));
}
#endif

void (*StkCeil)() = dStkCeil;

void dStkTrunc()
{
	g_argument1->d.real(int(g_argument1->d.real()));
	g_argument1->d.imag(int(g_argument1->d.imag()));
}

#if !defined(NO_FIXED_POINT_MATH)
void lStkTrunc()
{
	/* shifting and shifting back truncates positive numbers,
		so we make the numbers positive */
	int signx = sign(g_argument1->l.real());
	int signy = sign(g_argument1->l.imag());
	g_argument1->l.real(labs(g_argument1->l.real()));
	g_argument1->l.imag(labs(g_argument1->l.imag()));
	g_argument1->l.real((g_argument1->l.real()) >> g_bit_shift);
	g_argument1->l.imag((g_argument1->l.imag()) >> g_bit_shift);
	g_argument1->l.real((g_argument1->l.real()) << g_bit_shift);
	g_argument1->l.imag((g_argument1->l.imag()) << g_bit_shift);
	g_argument1->l.real(signx*g_argument1->l.real());
	g_argument1->l.imag(signy*g_argument1->l.imag());
}
#endif

void (*StkTrunc)() = dStkTrunc;

void dStkRound()
{
	g_argument1->d.real(floor(g_argument1->d.real() + .5));
	g_argument1->d.imag(floor(g_argument1->d.imag() + .5));
}

#if !defined(NO_FIXED_POINT_MATH)
void lStkRound()
{
	// Add .5 then truncate
	g_argument1->l.real(g_argument1->l.real() + (1L << g_bit_shift_minus_1));
	g_argument1->l.imag(g_argument1->l.imag() + (1L << g_bit_shift_minus_1));
	lStkFloor();
}
#endif

void (*StkRound)() = dStkRound;

void dStkZero()
{
	g_argument1->d.imag(0.0);
	g_argument1->d.real(0.0);
}

#if !defined(NO_FIXED_POINT_MATH)
void lStkZero()
{
	g_argument1->l.imag(0);
	g_argument1->l.real(0);
}
#endif

void (*StkZero)() = dStkZero;

void dStkOne()
{
	g_argument1->d.real(1.0);
	g_argument1->d.imag(0.0);
}

#if !defined(NO_FIXED_POINT_MATH)
void lStkOne()
{
	g_argument1->l.real(long(s_fudge));
	g_argument1->l.imag(0L);
}
#endif

void (*StkOne)() = dStkOne;


void dStkReal()
{
	g_argument1->d.imag(0.0);
}

#if !defined(NO_FIXED_POINT_MATH)
void lStkReal()
{
	g_argument1->l.imag(0L);
}
#endif

void (*StkReal)() = dStkReal;

void dStkImag()
{
	g_argument1->d.real(g_argument1->d.imag());
	g_argument1->d.imag(0.0);
}

#if !defined(NO_FIXED_POINT_MATH)
void lStkImag()
{
	g_argument1->l.real(g_argument1->l.imag());
	g_argument1->l.imag(0L);
}
#endif

void (*StkImag)() = dStkImag;

void dStkNeg()
{
	g_argument1->d = -g_argument1->d;
}

#if !defined(NO_FIXED_POINT_MATH)
void lStkNeg()
{
	g_argument1->l = -g_argument1->l;
}
#endif

void (*StkNeg)() = dStkNeg;

void dStkMul()
{
	g_argument2->d = (g_argument2->d)*(g_argument1->d);
	g_argument1--;
	g_argument2--;
}

#if !defined(NO_FIXED_POINT_MATH)
void lStkMul()
{
	long x = multiply(g_argument2->l.real(), g_argument1->l.real(), g_bit_shift) -
		multiply(g_argument2->l.imag(), g_argument1->l.imag(), g_bit_shift);
	long y = multiply(g_argument2->l.imag(), g_argument1->l.real(), g_bit_shift) +
		multiply(g_argument2->l.real(), g_argument1->l.imag(), g_bit_shift);
	g_argument2->l.real(x);
	g_argument2->l.imag(y);
	g_argument1--;
	g_argument2--;
}
#endif

void (*StkMul)() = dStkMul;

void dStkDiv()
{
	g_argument2->d = g_argument2->d/g_argument1->d;
	g_argument1--;
	g_argument2--;
}

#if !defined(NO_FIXED_POINT_MATH)
void lStkDiv()
{
	long mod = multiply(g_argument1->l.real(), g_argument1->l.real(), g_bit_shift) +
		multiply(g_argument1->l.imag(), g_argument1->l.imag(), g_bit_shift);
	long x = divide(g_argument1->l.real(), mod, g_bit_shift);
	long y = -divide(g_argument1->l.imag(), mod, g_bit_shift);
	long x2 = multiply(g_argument2->l.real(), x, g_bit_shift) - multiply(g_argument2->l.imag(), y, g_bit_shift);
	long y2 = multiply(g_argument2->l.imag(), x, g_bit_shift) + multiply(g_argument2->l.real(), y, g_bit_shift);
	g_argument2->l.real(x2);
	g_argument2->l.imag(y2);
	g_argument1--;
	g_argument2--;
}
#endif

void (*StkDiv)() = dStkDiv;

void dStkMod()
{
	g_argument1->d.real(norm(g_argument1->d));
	g_argument1->d.imag(0.0);
}

#if !defined(NO_FIXED_POINT_MATH)
void lStkMod()
{
	g_argument1->l.real(multiply(g_argument1->l.real(), g_argument1->l.real(), g_bit_shift) +
		multiply(g_argument1->l.imag(), g_argument1->l.imag(), g_bit_shift));
	if (g_argument1->l.real() < 0)
	{
		g_overflow = true;
	}
	g_argument1->l.imag(0L);
}

void lStkModOld()
{
	g_argument1->l.real(multiply(g_argument2->l.real(), g_argument1->l.real(), g_bit_shift) +
		multiply(g_argument2->l.imag(), g_argument1->l.imag(), g_bit_shift));
	if (g_argument1->l.real() < 0)
	{
		g_overflow = true;
	}
	g_argument1->l.imag(0L);
}
#endif

void (*StkMod)() = dStkMod;

void StkSto()
{
	g_formula_state.StackStore();
}

void Formula::StackStore()
{
	*m_store[m_store_ptr++] = *g_argument1;
}

void (*PtrStkSto)() = StkSto;

void StkLod()
{
	g_formula_state.StackLoad();
}

void Formula::StackLoad()
{
	g_argument1++;
	g_argument2++;
	*g_argument1 = *m_load[m_load_ptr++];
}

void StkClr()
{
	g_formula_state.StackClear();
}

void Formula::StackClear()
{
	m_argument_stack[0] = *g_argument1;
	g_argument1 = &m_argument_stack[0];
	g_argument2 = g_argument1;
	g_argument2--;
}

void (*PtrStkClr)() = StkClr;

void dStkFlip()
{
	double t;

	t = g_argument1->d.real();
	g_argument1->d.real(g_argument1->d.imag());
	g_argument1->d.imag(t);
}

#if !defined(NO_FIXED_POINT_MATH)
void lStkFlip()
{
	long t;

	t = g_argument1->l.real();
	g_argument1->l.real(g_argument1->l.imag());
	g_argument1->l.imag(t);
}
#endif

void (*StkFlip)() = dStkFlip;

void dStkSin()
{
	g_argument1->d = sin(g_argument1->d);
}

#if !defined(NO_FIXED_POINT_MATH)
void lStkSin()
{
	long x = g_argument1->l.real() >> s_delta16;
	long y = g_argument1->l.imag() >> s_delta16;
	long sinx;
	long cosx;
	SinCos086(x, &sinx, &cosx);
	long sinhy;
	long coshy;
	SinhCosh086(y, &sinhy, &coshy);
	g_argument1->l.real(multiply(sinx, coshy, s_shift_back));
	g_argument1->l.imag(multiply(cosx, sinhy, s_shift_back));
}
#endif

void (*StkSin)() = dStkSin;

/* The following functions are supported by both the parser and for fn
	variable replacement. */

void dStkTan()
{
	g_argument1->d.real(g_argument1->d.real()*2);
	g_argument1->d.imag(g_argument1->d.imag()*2);
	double const sinx = std::sin(g_argument1->d.real());
	double const cosx = std::cos(g_argument1->d.real());
	double sinhy;
	double coshy;
	FPUsinhcosh(g_argument1->d.imag(), &sinhy, &coshy);
	double denom = cosx + coshy;
	ChkFloatDenom(denom);
	g_argument1->d.real(sinx/denom);
	g_argument1->d.imag(sinhy/denom);
}

#if !defined(NO_FIXED_POINT_MATH)
void lStkTan()
{
	long x = (g_argument1->l.real() >> s_delta16)*2;
	long y = (g_argument1->l.imag() >> s_delta16)*2;
	long sinx;
	long cosx;
	SinCos086(x, &sinx, &cosx);
	long sinhy;
	long coshy;
	SinhCosh086(y, &sinhy, &coshy);
	long denom = cosx + coshy;
	ChkLongDenom(denom);
	g_argument1->l.real(divide(sinx, denom, g_bit_shift));
	g_argument1->l.imag(divide(sinhy, denom, g_bit_shift));
}
#endif

void (*StkTan)() = dStkTan;

void dStkTanh()
{
	g_argument1->d.real(g_argument1->d.real()*2);
	g_argument1->d.imag(g_argument1->d.imag()*2);
	double const siny = std::sin(g_argument1->d.imag());
	double const cosy = std::cos(g_argument1->d.imag());
	double sinhx;
	double coshx;
	FPUsinhcosh(g_argument1->d.real(), &sinhx, &coshx);
	double denom = coshx + cosy;
	ChkFloatDenom(denom);
	g_argument1->d.real(sinhx/denom);
	g_argument1->d.imag(siny/denom);
}

#if !defined(NO_FIXED_POINT_MATH)
void lStkTanh()
{
	long x = g_argument1->l.real() >> s_delta16;
	x <<= 1;
	long y = g_argument1->l.imag() >> s_delta16;
	y <<= 1;
	long siny;
	long cosy;
	SinCos086(y, &siny, &cosy);
	long sinhx;
	long coshx;
	SinhCosh086(x, &sinhx, &coshx);
	long denom = coshx + cosy;
	ChkLongDenom(denom);
	g_argument1->l.real(divide(sinhx, denom, g_bit_shift));
	g_argument1->l.imag(divide(siny, denom, g_bit_shift));
}
#endif

void (*StkTanh)() = dStkTanh;

void dStkCoTan()
{
	g_argument1->d.real(g_argument1->d.real()*2);
	g_argument1->d.imag(g_argument1->d.imag()*2);
	double const sinx = std::sin(g_argument1->d.real());
	double const cosx = std::cos(g_argument1->d.real());
	double sinhy;
	double coshy;
	FPUsinhcosh(g_argument1->d.imag(), &sinhy, &coshy);
	double denom = coshy - cosx;
	ChkFloatDenom(denom);
	g_argument1->d.real(sinx/denom);
	g_argument1->d.imag(-sinhy/denom);
}

#if !defined(NO_FIXED_POINT_MATH)
void lStkCoTan()
{
	long x = g_argument1->l.real() >> s_delta16;
	x <<= 1;
	long y = g_argument1->l.imag() >> s_delta16;
	y <<= 1;
	long sinx;
	long cosx;
	SinCos086(x, &sinx, &cosx);
	long sinhy;
	long coshy;
	SinhCosh086(y, &sinhy, &coshy);
	long denom = coshy - cosx;
	ChkLongDenom(denom);
	g_argument1->l.real(divide(sinx, denom, g_bit_shift));
	g_argument1->l.imag(-divide(sinhy, denom, g_bit_shift));
}
#endif

void (*StkCoTan)() = dStkCoTan;

void dStkCoTanh()
{
	g_argument1->d.real(g_argument1->d.real()*2);
	g_argument1->d.imag(g_argument1->d.imag()*2);
	double const siny = std::sin(g_argument1->d.imag());
	double const cosy = std::cos(g_argument1->d.imag());
	double sinhx;
	double coshx;
	FPUsinhcosh(g_argument1->d.real(), &sinhx, &coshx);
	double denom = coshx - cosy;
	ChkFloatDenom(denom);
	g_argument1->d.real(sinhx/denom);
	g_argument1->d.imag(-siny/denom);
}

#if !defined(NO_FIXED_POINT_MATH)
void lStkCoTanh()
{
	long x = g_argument1->l.real() >> s_delta16;
	x <<= 1;
	long y = g_argument1->l.imag() >> s_delta16;
	y <<= 1;
	long siny;
	long cosy;
	SinCos086(y, &siny, &cosy);
	long sinhx;
	long coshx;
	SinhCosh086(x, &sinhx, &coshx);
	long denom = coshx - cosy;
	ChkLongDenom(denom);
	g_argument1->l.real(divide(sinhx, denom, g_bit_shift));
	g_argument1->l.imag(-divide(siny, denom, g_bit_shift));
}
#endif

void (*StkCoTanh)() = dStkCoTanh;

/* The following functions are not directly used by the parser - support
	for the parser was not provided because the existing parser language
	represents these quite easily. They are used for fn variable support
	in miscres.c but are placed here because they follow the pattern of
	the other parser functions. */

void dStkRecip()
{
	double mod;
	mod = norm(g_argument1->d);
	ChkFloatDenom(mod);
	g_argument1->d.real( g_argument1->d.real()/mod);
	g_argument1->d.imag(-g_argument1->d.imag()/mod);
}

#if !defined(NO_FIXED_POINT_MATH)
void lStkRecip()
{
	long mod;
	mod = multiply(g_argument1->l.real(), g_argument1->l.real(), g_bit_shift)
		+ multiply(g_argument1->l.imag(), g_argument1->l.imag(), g_bit_shift);
	ChkLongDenom(mod);
	g_argument1->l.real( divide(g_argument1->l.real(), mod, g_bit_shift));
	g_argument1->l.imag(-divide(g_argument1->l.imag(), mod, g_bit_shift));
}
#endif

void StkIdent()  // do nothing - the function Z
{
}

void dStkSinh()
{
	double const siny = std::sin(g_argument1->d.imag());
	double const cosy = std::cos(g_argument1->d.imag());
	double sinhx;
	double coshx;
	FPUsinhcosh(g_argument1->d.real(), &sinhx, &coshx);
	g_argument1->d.real(sinhx*cosy);
	g_argument1->d.imag(coshx*siny);
}

#if !defined(NO_FIXED_POINT_MATH)
void lStkSinh()
{
	long x = g_argument1->l.real() >> s_delta16;
	long y = g_argument1->l.imag() >> s_delta16;
	long siny;
	long cosy;
	SinCos086(y, &siny, &cosy);
	long sinhx;
	long coshx;
	SinhCosh086(x, &sinhx, &coshx);
	g_argument1->l.real(multiply(cosy, sinhx, s_shift_back));
	g_argument1->l.imag(multiply(siny, coshx, s_shift_back));
}
#endif

void (*StkSinh)() = dStkSinh;

void dStkCos()
{
	double const sinx = std::sin(g_argument1->d.real());
	double const cosx = std::cos(g_argument1->d.real());
	double sinhy;
	double coshy;
	FPUsinhcosh(g_argument1->d.imag(), &sinhy, &coshy);
	g_argument1->d.real(cosx*coshy);
	g_argument1->d.imag(-sinx*sinhy);
}

#if !defined(NO_FIXED_POINT_MATH)
void lStkCos()
{
	long x = g_argument1->l.real() >> s_delta16;
	long y = g_argument1->l.imag() >> s_delta16;
	long sinx;
	long cosx;
	SinCos086(x, &sinx, &cosx);
	long sinhy;
	long coshy;
	SinhCosh086(y, &sinhy, &coshy);
	g_argument1->l.real(multiply(cosx, coshy, s_shift_back));
	g_argument1->l.imag(-multiply(sinx, sinhy, s_shift_back));
}
#endif

void (*StkCos)() = dStkCos;

// Bogus version of cos, to replicate bug which was in regular cos till v16:

void dStkCosXX()
{
	dStkCos();
	g_argument1->d = conj(g_argument1->d);
}

#if !defined(NO_FIXED_POINT_MATH)
void lStkCosXX()
{
	lStkCos();
	g_argument1->l = conj(g_argument1->l);
}
#endif

void (*StkCosXX)() = dStkCosXX;

void dStkCosh()
{
	double const siny = std::sin(g_argument1->d.imag());
	double const cosy = std::cos(g_argument1->d.imag());
	double sinhx;
	double coshx;
	FPUsinhcosh(g_argument1->d.real(), &sinhx, &coshx);
	g_argument1->d.real(coshx*cosy);
	g_argument1->d.imag(sinhx*siny);
}

#if !defined(NO_FIXED_POINT_MATH)
void lStkCosh()
{
	long x = g_argument1->l.real() >> s_delta16;
	long y = g_argument1->l.imag() >> s_delta16;
	long siny;
	long cosy;
	SinCos086(y, &siny, &cosy);
	long sinhx;
	long coshx;
	SinhCosh086(x, &sinhx, &coshx);
	g_argument1->l.real(multiply(cosy, coshx, s_shift_back));
	g_argument1->l.imag(multiply(siny, sinhx, s_shift_back));
}
#endif

void (*StkCosh)() = dStkCosh;

void dStkASin()
{
	g_argument1->d = asin(g_argument1->d);
}

#if !defined(NO_FIXED_POINT_MATH)
void lStkASin()
{
	lStkFunct(dStkASin);
}
#endif

void (*StkASin)() = dStkASin;

void dStkASinh()
{
	g_argument1->d = asinh(g_argument1->d);
}

#if !defined(NO_FIXED_POINT_MATH)
void lStkASinh()
{
	lStkFunct(dStkASinh);
}
#endif

void (*StkASinh)() = dStkASinh;

void dStkACos()
{
	g_argument1->d = acos(g_argument1->d);
}

#if !defined(NO_FIXED_POINT_MATH)
void lStkACos()
{
	lStkFunct(dStkACos);
}
#endif

void (*StkACos)() = dStkACos;

void dStkACosh()
{
	g_argument1->d = acosh(g_argument1->d);
}

#if !defined(NO_FIXED_POINT_MATH)
void lStkACosh()
{
	lStkFunct(dStkACosh);
}
#endif

void (*StkACosh)() = dStkACosh;

void dStkATan()
{
	g_argument1->d = atan(g_argument1->d);
}

#if !defined(NO_FIXED_POINT_MATH)
void lStkATan()
{
	lStkFunct(dStkATan);
}
#endif

void (*StkATan)() = dStkATan;

void dStkATanh()
{
	g_argument1->d = atanh(g_argument1->d);
}

#if !defined(NO_FIXED_POINT_MATH)
void lStkATanh()
{
	lStkFunct(dStkATanh);
}
#endif

void (*StkATanh)() = dStkATanh;

void dStkSqrt()
{
	g_argument1->d = ComplexSqrtFloat(g_argument1->d.real(), g_argument1->d.imag());
}

#if !defined(NO_FIXED_POINT_MATH)
void lStkSqrt()
{
	g_argument1->l = ComplexSqrtLong(g_argument1->l.real(), g_argument1->l.imag());
}
#endif

void (*StkSqrt)() = dStkSqrt;

void dStkCAbs()
{
	g_argument1->d.real(std::sqrt(norm(g_argument1->d)));
	g_argument1->d.imag(0.0);
}

#if !defined(NO_FIXED_POINT_MATH)
void lStkCAbs()
{
	lStkFunct(dStkCAbs);
}
#endif

void (*StkCAbs)() = dStkCAbs;

void dStkLT()
{
	g_argument2->d.real(double(g_argument2->d.real() < g_argument1->d.real()));
	g_argument2->d.imag(0.0);
	g_argument1--;
	g_argument2--;
}

#if !defined(NO_FIXED_POINT_MATH)
void lStkLT()
{
	g_argument2->l.real(long(g_argument2->l.real() < g_argument1->l.real()) << g_bit_shift);
	g_argument2->l.imag(0L);
	g_argument1--;
	g_argument2--;
}
#endif

void (*StkLT)() = dStkLT;

void dStkGT()
{
	g_argument2->d.real(double(g_argument2->d.real() > g_argument1->d.real()));
	g_argument2->d.imag(0.0);
	g_argument1--;
	g_argument2--;
}

#if !defined(NO_FIXED_POINT_MATH)
void lStkGT()
{
	g_argument2->l.real(long(g_argument2->l.real() > g_argument1->l.real()) << g_bit_shift);
	g_argument2->l.imag(0L);
	g_argument1--;
	g_argument2--;
}
#endif

void (*StkGT)() = dStkGT;

void dStkLTE()
{
	g_argument2->d.real(double(g_argument2->d.real() <= g_argument1->d.real()));
	g_argument2->d.imag(0.0);
	g_argument1--;
	g_argument2--;
}

#if !defined(NO_FIXED_POINT_MATH)
void lStkLTE()
{
	g_argument2->l.real(long(g_argument2->l.real() <= g_argument1->l.real()) << g_bit_shift);
	g_argument2->l.imag(0L);
	g_argument1--;
	g_argument2--;
}
#endif

void (*StkLTE)() = dStkLTE;

void dStkGTE()
{
	g_argument2->d.real(double(g_argument2->d.real() >= g_argument1->d.real()));
	g_argument2->d.imag(0.0);
	g_argument1--;
	g_argument2--;
}

#if !defined(NO_FIXED_POINT_MATH)
void lStkGTE()
{
	g_argument2->l.real(long(g_argument2->l.real() >= g_argument1->l.real()) << g_bit_shift);
	g_argument2->l.imag(0L);
	g_argument1--;
	g_argument2--;
}
#endif

void (*StkGTE)() = dStkGTE;

void dStkEQ()
{
	g_argument2->d.real(double(g_argument2->d.real() == g_argument1->d.real()));
	g_argument2->d.imag(0.0);
	g_argument1--;
	g_argument2--;
}

#if !defined(NO_FIXED_POINT_MATH)
void lStkEQ()
{
	g_argument2->l.real(long(g_argument2->l.real() == g_argument1->l.real()) << g_bit_shift);
	g_argument2->l.imag(0L);
	g_argument1--;
	g_argument2--;
}
#endif

void (*StkEQ)() = dStkEQ;

void dStkNE()
{
	g_argument2->d.real(double(g_argument2->d.real() != g_argument1->d.real()));
	g_argument2->d.imag(0.0);
	g_argument1--;
	g_argument2--;
}

#if !defined(NO_FIXED_POINT_MATH)
void lStkNE()
{
	g_argument2->l.real(long(g_argument2->l.real() != g_argument1->l.real()) << g_bit_shift);
	g_argument2->l.imag(0L);
	g_argument1--;
	g_argument2--;
}
#endif

void (*StkNE)() = dStkNE;

void dStkOR()
{
	g_argument2->d.real(double(g_argument2->d.real() || g_argument1->d.real()));
	g_argument2->d.imag(0.0);
	g_argument1--;
	g_argument2--;
}

#if !defined(NO_FIXED_POINT_MATH)
void lStkOR()
{
	g_argument2->l.real(long(g_argument2->l.real() || g_argument1->l.real()) << g_bit_shift);
	g_argument2->l.imag(0L);
	g_argument1--;
	g_argument2--;
}
#endif

void (*StkOR)() = dStkOR;

void dStkAND()
{
	g_argument2->d.real(double(g_argument2->d.real() && g_argument1->d.real()));
	g_argument2->d.imag(0.0);
	g_argument1--;
	g_argument2--;
}

#if !defined(NO_FIXED_POINT_MATH)
void lStkAND()
{
	g_argument2->l.real(long(g_argument2->l.real() && g_argument1->l.real()) << g_bit_shift);
	g_argument2->l.imag(0L);
	g_argument1--;
	g_argument2--;
}
#endif

void (*StkAND)() = dStkAND;
void dStkLog()
{
	g_argument1->d = log(g_argument1->d);
}

#if !defined(NO_FIXED_POINT_MATH)
void lStkLog()
{
	lStkFunct(dStkLog);
}
#endif

void (*StkLog)() = dStkLog;

void dStkExp()
{
	g_argument1->d = exp(g_argument1->d);
}

#if !defined(NO_FIXED_POINT_MATH)
void lStkExp()
{
	lStkFunct(dStkExp);
}
#endif

void (*StkExp)() = dStkExp;

void dStkPwr()
{
	g_argument2->d = pow(g_argument2->d, g_argument1->d);
	g_argument1--;
	g_argument2--;
}

#if !defined(NO_FIXED_POINT_MATH)
void lStkPwr()
{
	ComplexD x = pow(ComplexFudgeToDouble(g_argument2->l), ComplexFudgeToDouble(g_argument1->l));
	if (std::abs(x.real()) < g_fudge_limit
		&& std::abs(x.imag()) < g_fudge_limit)
	{
		g_argument2->l = ComplexDoubleToFudge(x);
	}
	else
	{
		g_overflow = true;
	}
	g_argument1--;
	g_argument2--;
}
#endif

void (*StkPwr)() = dStkPwr;

void Formula::end_init()
{
	m_last_init_op = m_op_index;
	m_initial_jump_index = m_jump_index;
}

void StkJump()
{
	g_formula_state.StackJump();
}

void Formula::StackJump()
{
	m_op_index =  m_jump_control[m_jump_index].ptrs.JumpOpPtr;
	m_load_ptr = m_jump_control[m_jump_index].ptrs.JumpLodPtr;
	m_store_ptr = m_jump_control[m_jump_index].ptrs.JumpStoPtr;
	m_jump_index = m_jump_control[m_jump_index].DestJumpIndex;
}

void dStkJumpOnFalse()
{
	return g_formula_state.StackJumpOnFalse_d();
}

void Formula::StackJumpOnFalse_d()
{
	if (g_argument1->d.real() == 0)
	{
		StkJump();
	}
	else
	{
		m_jump_index++;
	}
}

void lStkJumpOnFalse()
{
	g_formula_state.StackJumpOnFalse_l();
}

void Formula::StackJumpOnFalse_l()
{
	if (g_argument1->l.real() == 0)
	{
		StkJump();
	}
	else
	{
		m_jump_index++;
	}
}

void (*StkJumpOnFalse)() = dStkJumpOnFalse;

void dStkJumpOnTrue()
{
	g_formula_state.StackJumpOnTrue_d();
}

void Formula::StackJumpOnTrue_d()
{
	if (g_argument1->d.real())
	{
		StkJump();
	}
	else
	{
		m_jump_index++;
	}
}

void lStkJumpOnTrue()
{
	g_formula_state.StackJumpOnTrue_l();
}

void Formula::StackJumpOnTrue_l()
{
	if (g_argument1->l.real())
	{
		StkJump();
	}
	else
	{
		m_jump_index++;
	}
}

void (*StkJumpOnTrue)() = dStkJumpOnTrue;

void StkJumpLabel()
{
	g_formula_state.StackJumpLabel();
}

void Formula::StackJumpLabel()
{
	m_jump_index++;
}

static int count_white_space(const char *text)
{
	int n = 0;
	while (text[n] && isspace(text[n]))
	{
		n++;
	}
	return n;
}

// detect if constant is part of a (a, b) construct
static bool is_constant_pair(const char *text)
{
	// skip past first number
	int n;
	for (n = 0; isdigit(text[n]) || text[n] == '.'; n++)
	{
	}
	if (text[n] == ',')
	{
		int j = n + count_white_space(&text[n + 1]) + 1;
		if (isdigit(text[j])
			|| (text[j] == '-' && (isdigit(text[j + 1]) || text[j + 1] == '.'))
			|| text[j] == '.')
		{
			return true;
		}
	}
	return false;
}

ConstArg *Formula::is_constant(const char *text, int length)
{
	ComplexD z;
	unsigned j;
	// next line enforces variable vs constant naming convention
	for (int n = 0; n < m_parser_vsp; n++)
	{
		if (m_variables[n].name_length == length)
		{
			if (!strnicmp(m_variables[n].name, text, length))
			{
				switch (n)
				{
				case VARIABLE_P1:		m_uses_p1 = true;		break;
				case VARIABLE_P2:		m_uses_p2 = true;		break;
				case VARIABLE_P3:		m_uses_p3 = true;		break;
				case VARIABLE_P4:		m_uses_p4 = true;		break;
				case VARIABLE_P5:		m_uses_p5 = true;		break;
				case VARIABLE_IS_MAND:	m_uses_is_mand = true;	break;
				case VARIABLE_RAND:		RandomSeed();			break;
				case VARIABLE_SCRN_PIX:
				case VARIABLE_SCRN_MAX:
				case VARIABLE_MAX_IT:
#if !defined(NO_FIXED_POINT_MATH)
					if (m_math_type == FIXED_POINT_MATH)
					{
						driver_unget_key('f');
					}
#endif
					break;
				}
				if (!is_constant_pair(text))
				{
					return &m_variables[n];
				}
			}
		}
	}
	m_variables[m_parser_vsp].name = text;
	m_variables[m_parser_vsp].name_length = length;
	m_variables[m_parser_vsp].argument.d.real(0.0);
	m_variables[m_parser_vsp].argument.d.imag(0.0);

	// m_variables[m_parser_vsp].a should already be zeroed out
	switch (m_math_type)
	{
#if !defined(NO_FIXED_POINT_MATH)
	case FIXED_POINT_MATH:
		m_variables[m_parser_vsp].argument.l.real(0);
		m_variables[m_parser_vsp].argument.l.imag(0);
		break;
#endif
	}

	if (isdigit(text[0])
		|| (text[0] == '-' && (isdigit(text[1]) || text[1] == '.'))
		|| text[0] == '.')
	{
		if (s_ops[m_posp-1].function == StkNeg)
		{
			m_posp--;
			text -= 1;
			m_initial_n--;
			m_variables[m_parser_vsp].name_length++;
		}
		int n;
		for (n = 1; isdigit(text[n]) || text[n] == '.'; n++)
		{
		}
		if (text[n] == ',')
		{
			j = n + count_white_space(&text[n + 1]) + 1;
			if (isdigit(text[j])
				|| (text[j] == '-' && (isdigit(text[j + 1]) || text[j + 1] == '.'))
				|| text[j] == '.')
			{
				z.imag(atof(&text[j]));
				for (; isdigit(text[j]) || text[j] == '.' || text[j] == '-'; j++)
				{
				}
				m_variables[m_parser_vsp].name_length = j;
			}
			else
			{
				z.imag(0.0);
			}
		}
		else
		{
			z.imag(0.0);
		}
		z.real(atof(text));
		switch (m_math_type)
		{
		case FLOATING_POINT_MATH:
			m_variables[m_parser_vsp].argument.d = z;
			break;
#if !defined(NO_FIXED_POINT_MATH)
		case FIXED_POINT_MATH:
			m_variables[m_parser_vsp].argument.l = ComplexDoubleToFudge(z);
			break;
#endif
		}
		m_variables[m_parser_vsp].name = text;
	}
	return &m_variables[m_parser_vsp++];
}

struct function_list_item
{
	const char *name;
	void (**function)();
	FormulaTokenType tokenType;
};

void (*StkTrig0)() = dStkSin;
void (*StkTrig1)() = dStkSqr;
void (*StkTrig2)() = dStkSinh;
void (*StkTrig3)() = dStkCosh;

struct jump_list_item
{
	const char *name;
	JumpType jumpType;
};

static const jump_list_item s_jump_list[] =
{
	{ "if", JUMPTYPE_IF },
	{ "elseif", JUMPTYPE_ELSEIF },
	{ "else", JUMPTYPE_ELSE },
	{ "endif", JUMPTYPE_ENDIF }
};

static JumpType is_jump_keyword(const char *Str, int Len)
{
	for (int i = 0; i < NUM_OF(s_jump_list); i++)
	{
		if (int_strlen(s_jump_list[i].name) == Len)
		{
			if (!strnicmp(s_jump_list[i].name, Str, Len))
			{
				return s_jump_list[i].jumpType;
			}
		}
	}
	return JUMPTYPE_NONE;
}

#define FNITEM_PARAM_FN(name_, function_) \
	{ name_, &function_, TOKENTYPE_PARAMETER_FUNCTION }
#define FNITEM_FN(name_, function_) \
	{ name_, &function_, TOKENTYPE_FUNCTION }
static function_list_item s_function_list[] =
{
	FNITEM_FN("sin", StkSin),
	FNITEM_FN("sinh", StkSinh),
	FNITEM_FN("cos", StkCos),
	FNITEM_FN("cosh", StkCosh),
	FNITEM_FN("sqr", StkSqr),
	FNITEM_FN("log", StkLog),
	FNITEM_FN("exp", StkExp),
	FNITEM_FN("abs", StkAbs),
	FNITEM_FN("conj", StkConj),
	FNITEM_FN("real", StkReal),
	FNITEM_FN("imag", StkImag),
	FNITEM_PARAM_FN("fn1", StkTrig0),
	FNITEM_PARAM_FN("fn2", StkTrig1),
	FNITEM_PARAM_FN("fn3", StkTrig2),
	FNITEM_PARAM_FN("fn4", StkTrig3),
	FNITEM_FN("flip", StkFlip),
	FNITEM_FN("tan", StkTan),
	FNITEM_FN("tanh", StkTanh),
	FNITEM_FN("cotan", StkCoTan),
	FNITEM_FN("cotanh", StkCoTanh),
	FNITEM_FN("cosxx", StkCosXX),
	FNITEM_FN("srand", StkSRand),
	FNITEM_FN("asin", StkASin),
	FNITEM_FN("asinh", StkASinh),
	FNITEM_FN("acos", StkACos),
	FNITEM_FN("acosh", StkACosh),
	FNITEM_FN("atan", StkATan),
	FNITEM_FN("atanh", StkATanh),
	FNITEM_FN("sqrt", StkSqrt),
	FNITEM_FN("cabs", StkCAbs),
	FNITEM_FN("floor", StkFloor),
	FNITEM_FN("ceil", StkCeil),
	FNITEM_FN("trunc", StkTrunc),
	FNITEM_FN("round", StkRound),
};
#undef FNITEM_FN
#undef FNITEM_PARAM_FN

enum OperatorType
{
	OPERATOR_COMMA = 0,
	OPERATOR_NOT_EQUAL,
	OPERATOR_ASSIGNMENT,
	OPERATOR_EQUAL,
	OPERATOR_LESS,
	OPERATOR_LESS_EQUAL,
	OPERATOR_GREATER,
	OPERATOR_GREATER_EQUAL,
	OPERATOR_MODULUS,
	OPERATOR_OR,
	OPERATOR_AND,
	OPERATOR_COLON,
	OPERATOR_PLUS,
	OPERATOR_MINUS,
	OPERATOR_MULTIPLY,
	OPERATOR_DIVIDE,
	OPERATOR_RAISE_POWER
};

struct operator_list_item
{
	const char *name;
	OperatorType type;
};

static const operator_list_item s_operator_list[] =
{
	{ ",",	OPERATOR_COMMA },	// 0
	{ "!=",	OPERATOR_NOT_EQUAL },	// 1
	{ "=",	OPERATOR_ASSIGNMENT },	// 2
	{ "==",	OPERATOR_EQUAL },	// 3
	{ "<",	OPERATOR_LESS },	// 4
	{ "<=",	OPERATOR_LESS_EQUAL },	// 5
	{ ">",	OPERATOR_GREATER },	// 6
	{ ">=",	OPERATOR_GREATER_EQUAL },	// 7
	{ "|",	OPERATOR_MODULUS },	// 8
	{ "||",	OPERATOR_OR },	// 9
	{ "&&",	OPERATOR_AND },	// 10
	{ ":",	OPERATOR_COLON },	// 11
	{ "+",	OPERATOR_PLUS },	// 12
	{ "-",	OPERATOR_MINUS },	// 13
	{ "*",	OPERATOR_MULTIPLY },	// 14
	{ "/",	OPERATOR_DIVIDE },	// 15
	{ "^",	OPERATOR_RAISE_POWER },	// 16
};

static void not_a_function()
{
}

static void function_not_found()
{
}

// determine if text names a function and if so which one
static int which_function(const char *text, int length)
{
	if ((length != 3) || strnicmp(text, "fn", 2))
	{
		return 0;
	}

	int function_number = atoi(text + 2);
	if (function_number < 1 || function_number > 4)
	{
		return 0;
	}

	return function_number;
}

t_function *Formula::is_function(const char *text, int length)
{
	unsigned n = count_white_space(&text[length]);
	if (text[length + n] == '(')
	{
		for (int i = 0; i < NUM_OF(s_function_list); i++)
		{
			if (int_strlen(s_function_list[i].name) == length)
			{
				if (!strnicmp(s_function_list[i].name, text, length))
				{
					// count function variables
					int function_number = which_function(text, length);
					if (function_number != 0)
					{
						if (function_number > m_max_function_number)
						{
							m_max_function_number = function_number;
						}
					}
					return *s_function_list[i].function;
				}
			}
		}
		return function_not_found;
	}
	return not_a_function;
}

void Formula::sort_prec()
{
	int current = m_next_operation++;
	while (s_ops[current].prec > s_ops[m_next_operation].prec && m_next_operation < m_posp)
	{
		sort_prec();
	}
	m_functions[m_op_index++] = s_ops[current].function;
}

int Formula::get_prec(int offset, int store_count)
{
	return offset - (m_parenthesis_count + store_count)*15;
}

void Formula::store_function(void (*function)(), int p)
{
	s_ops[m_posp].function = function;
	s_ops[m_posp++].prec = p;
}

void Formula::store_function(void (*function)(), int offset, int store_count)
{
	store_function(function, get_prec(offset, store_count));
}

void Formula::parse_string_set_math()
{
	switch (m_math_type)
	{
	case FLOATING_POINT_MATH:
		StkAdd = dStkAdd;
		StkSub = dStkSub;
		StkNeg = dStkNeg;
		StkMul = dStkMul;
		StkSin = dStkSin;
		StkSinh = dStkSinh;
		StkLT = dStkLT;
		StkLTE = dStkLTE;
		StkMod = dStkMod;
		StkSqr = dStkSqr;
		StkCos = dStkCos;
		StkCosh = dStkCosh;
		StkLog = dStkLog;
		StkExp = dStkExp;
		StkPwr = dStkPwr;
		StkDiv = dStkDiv;
		StkAbs = dStkAbs;
		StkReal = dStkReal;
		StkImag = dStkImag;
		StkConj = dStkConj;
		StkTrig0 = g_trig0_d;
		StkTrig1 = g_trig1_d;
		StkTrig2 = g_trig2_d;
		StkTrig3 = g_trig3_d;
		StkFlip = dStkFlip;
		StkTan = dStkTan;
		StkTanh = dStkTanh;
		StkCoTan = dStkCoTan;
		StkCoTanh = dStkCoTanh;
		StkCosXX = dStkCosXX;
		StkGT  = dStkGT;
		StkGTE = dStkGTE;
		StkEQ  = dStkEQ;
		StkNE  = dStkNE;
		StkAND = dStkAND;
		StkOR  = dStkOR;
		StkSRand = dStkSRand;
		StkASin = dStkASin;
		StkASinh = dStkASinh;
		StkACos = dStkACos;
		StkACosh = dStkACosh;
		StkATan = dStkATan;
		StkATanh = dStkATanh;
		StkCAbs = dStkCAbs;
		StkSqrt = dStkSqrt;
		StkZero = dStkZero;
		StkFloor = dStkFloor;
		StkCeil = dStkCeil;
		StkTrunc = dStkTrunc;
		StkRound = dStkRound;
		StkJumpOnTrue  = dStkJumpOnTrue;
		StkJumpOnFalse = dStkJumpOnFalse;
		StkOne = dStkOne;
		break;
#if !defined(NO_FIXED_POINT_MATH)
	case FIXED_POINT_MATH:
		s_delta16 = g_bit_shift - 16;
		s_shift_back = 32 - g_bit_shift;
		StkAdd = lStkAdd;
		StkSub = lStkSub;
		StkNeg = lStkNeg;
		StkMul = lStkMul;
		StkSin = lStkSin;
		StkSinh = lStkSinh;
		StkLT = lStkLT;
		StkLTE = lStkLTE;
		StkMod = lStkMod;
		StkSqr = lStkSqr;
		StkCos = lStkCos;
		StkCosh = lStkCosh;
		StkLog = lStkLog;
		StkExp = lStkExp;
		StkPwr = lStkPwr;
		StkDiv = lStkDiv;
		StkAbs = lStkAbs;
		StkReal = lStkReal;
		StkImag = lStkImag;
		StkConj = lStkConj;
		StkTrig0 = g_trig0_l;
		StkTrig1 = g_trig1_l;
		StkTrig2 = g_trig2_l;
		StkTrig3 = g_trig3_l;
		StkFlip = lStkFlip;
		StkTan  = lStkTan;
		StkTanh  = lStkTanh;
		StkCoTan  = lStkCoTan;
		StkCoTanh  = lStkCoTanh;
		StkCosXX = lStkCosXX;
		StkGT  = lStkGT;
		StkGTE = lStkGTE;
		StkEQ  = lStkEQ;
		StkNE  = lStkNE;
		StkAND = lStkAND;
		StkOR  = lStkOR;
		StkSRand = lStkSRand;
		StkASin = lStkASin;
		StkACos = lStkACos;
		StkACosh = lStkACosh;
		StkATan = lStkATan;
		StkATanh = lStkATanh;
		StkCAbs = lStkCAbs;
		StkSqrt = lStkSqrt;
		StkZero = lStkZero;
		StkFloor = lStkFloor;
		StkCeil = lStkCeil;
		StkTrunc = lStkTrunc;
		StkRound = lStkRound;
		StkJumpOnTrue  = lStkJumpOnTrue;
		StkJumpOnFalse = lStkJumpOnFalse;
		StkOne = lStkOne;
		break;
#endif
	}
}
void Formula::parse_string_set_constants()
{
	for (m_parser_vsp = 0; m_parser_vsp < NUM_OF(s_constants); m_parser_vsp++)
	{
		m_variables[m_parser_vsp].name = s_constants[m_parser_vsp].name;
		m_variables[m_parser_vsp].name_length = int_strlen(s_constants[m_parser_vsp].name);
	}
}
void Formula::parse_string_set_center_magnification_variables()
{
	double center_real;
	double center_imag;
	LDBL magnification;
	double x_magnification_factor;
	double rotation;
	double skew;
	convert_center_mag(&center_real, &center_imag, &magnification, &x_magnification_factor, &rotation, &skew);
	m_variables[VARIABLE_RAND].argument.d.real(0.0);
	m_variables[VARIABLE_RAND].argument.d.imag(0.0);
	m_variables[VARIABLE_SCRN_MAX].argument.d.real(double(g_x_dots));
	m_variables[VARIABLE_SCRN_MAX].argument.d.imag(double(g_y_dots));
	m_variables[VARIABLE_MAX_IT].argument.d.real(double(g_max_iteration));
	m_variables[VARIABLE_MAX_IT].argument.d.imag(0);
	m_variables[VARIABLE_IS_MAND].argument.d.real(g_is_mandelbrot ? 1.0 : 0.0);
	m_variables[VARIABLE_IS_MAND].argument.d.imag(0);
	m_variables[VARIABLE_CENTER].argument.d.real(center_real);
	m_variables[VARIABLE_CENTER].argument.d.imag(center_imag);
	m_variables[VARIABLE_MAG_X_MAG].argument.d.real(double(magnification));
	m_variables[VARIABLE_MAG_X_MAG].argument.d.imag(x_magnification_factor);
	m_variables[VARIABLE_ROT_SKEW].argument.d.real(rotation);
	m_variables[VARIABLE_ROT_SKEW].argument.d.imag(skew);
}
void Formula::parse_string_set_parameters_float()
{
	m_variables[VARIABLE_P1].argument.d.real(g_parameters[P1_REAL]);
	m_variables[VARIABLE_P1].argument.d.imag(g_parameters[P1_IMAG]);
	m_variables[VARIABLE_P2].argument.d.real(g_parameters[P2_REAL]);
	m_variables[VARIABLE_P2].argument.d.imag(g_parameters[P2_IMAG]);
	m_variables[VARIABLE_PI].argument.d.real(MathUtil::Pi);
	m_variables[VARIABLE_PI].argument.d.imag(0.0);
	m_variables[VARIABLE_E].argument.d.real(MathUtil::e);
	m_variables[VARIABLE_E].argument.d.imag(0.0);
	m_variables[VARIABLE_P3].argument.d.real(g_parameters[P3_REAL]);
	m_variables[VARIABLE_P3].argument.d.imag(g_parameters[P3_IMAG]);
	m_variables[VARIABLE_P4].argument.d.real(g_parameters[P4_REAL]);
	m_variables[VARIABLE_P4].argument.d.imag(g_parameters[P4_IMAG]);
	m_variables[VARIABLE_P5].argument.d.real(g_parameters[P5_REAL]);
	m_variables[VARIABLE_P5].argument.d.imag(g_parameters[P5_IMAG]);
}
void Formula::parse_string_set_parameters_int()
{
#if !defined(NO_FIXED_POINT_MATH)
	m_variables[VARIABLE_P1].argument.l.real(DoubleToFudge(g_parameters[P1_REAL]));
	m_variables[VARIABLE_P1].argument.l.imag(DoubleToFudge(g_parameters[P1_IMAG]));
	m_variables[VARIABLE_P2].argument.l.real(DoubleToFudge(g_parameters[P2_REAL]));
	m_variables[VARIABLE_P2].argument.l.imag(DoubleToFudge(g_parameters[P2_IMAG]));
	m_variables[VARIABLE_PI].argument.l.real(DoubleToFudge(MathUtil::Pi));
	m_variables[VARIABLE_PI].argument.l.imag(0L);
	m_variables[VARIABLE_E].argument.l.real(DoubleToFudge(MathUtil::e));
	m_variables[VARIABLE_E].argument.l.imag(0L);
	m_variables[VARIABLE_P3].argument.l.real(DoubleToFudge(g_parameters[P3_REAL]));
	m_variables[VARIABLE_P3].argument.l.imag(DoubleToFudge(g_parameters[P3_IMAG]));
	m_variables[VARIABLE_SCRN_MAX].argument.l.real(g_x_dots << g_bit_shift);
	m_variables[VARIABLE_SCRN_MAX].argument.l.imag(g_y_dots << g_bit_shift);
	m_variables[VARIABLE_MAX_IT].argument.l.real(g_max_iteration << g_bit_shift);
	m_variables[VARIABLE_MAX_IT].argument.l.imag(0L);
	m_variables[VARIABLE_IS_MAND].argument.l.real((g_is_mandelbrot ? 1 : 0) << g_bit_shift);
	m_variables[VARIABLE_IS_MAND].argument.l.imag(0L);
	m_variables[VARIABLE_CENTER].argument.l = ComplexDoubleToFudge(m_variables[VARIABLE_CENTER].argument.d);
	m_variables[VARIABLE_MAG_X_MAG].argument.l = ComplexDoubleToFudge(m_variables[VARIABLE_MAG_X_MAG].argument.d);
	m_variables[VARIABLE_ROT_SKEW].argument.l = ComplexDoubleToFudge(m_variables[VARIABLE_ROT_SKEW].argument.d);
	m_variables[VARIABLE_P4].argument.l.real(DoubleToFudge(g_parameters[P4_REAL]));
	m_variables[VARIABLE_P4].argument.l.imag(DoubleToFudge(g_parameters[P4_IMAG]));
	m_variables[VARIABLE_P5].argument.l.real(DoubleToFudge(g_parameters[P5_REAL]));
	m_variables[VARIABLE_P5].argument.l.imag(DoubleToFudge(g_parameters[P5_IMAG]));
#endif
}

void Formula::parse_string_set_variables()
{
	parse_string_set_constants();
	parse_string_set_center_magnification_variables();
	switch (m_math_type)
	{
	case FLOATING_POINT_MATH:
		parse_string_set_parameters_float();
		break;
	case FIXED_POINT_MATH:
		parse_string_set_parameters_int();
		break;
	}
}
bool Formula::parse_string(const char *text, int pass)
{
	int modulus_flag = 999;
	int store_count = 0;
	int modulus[20];
	int modulus_stack = 0;
	s_random.set_random(false);
	s_random.set_randomized(false);
	m_uses_jump = false;
	m_jump_index = 0;
	if (!m_store || !m_load || !m_functions)
	{
		stop_message(STOPMSG_NORMAL, error_messages(PE_INSUFFICIENT_MEM_FOR_TYPE_FORMULA));
		return true;
	}
	parse_string_set_math();
	m_max_function_number = 0;
	parse_string_set_variables();
	m_last_init_op = 0;
	m_parenthesis_count = 0;
	m_op_index = 0;
	m_load_ptr = 0;
	m_store_ptr = 0;
	m_posp = 0;
	m_expecting_arg = true;
	for (int n = 0; text[n]; n++)
	{
		m_initial_n = n;
		switch (text[n])
		{
		case ' ':
		case '\t':
		case '\r':
		case '\n':
			break;
		case '(':
			m_parenthesis_count++;
			break;
		case ')':
			m_parenthesis_count--;
			break;
		case '|':
			if (text[n + 1] == '|')
			{
				m_expecting_arg = true;
				n++;
				store_function(StkOR, 7, store_count);
			}
			else if (modulus_flag == m_parenthesis_count-1)
			{
				m_parenthesis_count--;
				modulus_flag = modulus[--modulus_stack];
			}
			else
			{
				modulus[modulus_stack++] = modulus_flag;
				store_function(StkMod, 2, store_count);
				modulus_flag = m_parenthesis_count++;
			}
			break;
		case ',':
		case ';':
			if (!m_expecting_arg)
			{
				m_expecting_arg = true;
				store_function(0, 15);
				store_function(StkClr, -30000);
				store_count = 0;
				m_parenthesis_count = 0;
			}
			break;
		case ':':
			m_expecting_arg = true;
			store_function(0, 15);
			store_function(EndInit, -30000);
			store_count = 0;
			m_parenthesis_count = 0;
			m_last_init_op = 10000;
			break;
		case '+':
			m_expecting_arg = true;
			store_function(StkAdd, 4, store_count);
			break;
		case '-':
			if (m_expecting_arg)
			{
				store_function(StkNeg, 2, store_count);
			}
			else
			{
				store_function(StkSub, 4, store_count);
				m_expecting_arg = true;
			}
			break;
		case '&':
			m_expecting_arg = true;
			n++;
			store_function(StkAND, 7, store_count);
			break;
		case '!':
			m_expecting_arg = true;
			n++;
			store_function(StkNE, 6, store_count);
			break;
		case '<':
			m_expecting_arg = true;
			{
				void (*function)() = 0;
				if (text[n + 1] == '=')
				{
					n++;
					function = StkLTE;
				}
				else
				{
					function = StkLT;
				}
				store_function(function, 6, store_count);
			}
			break;
		case '>':
			m_expecting_arg = true;
			{
				void (*function)() = 0;
				if (text[n + 1] == '=')
				{
					n++;
					function = StkGTE;
				}
				else
				{
					function = StkGT;
				}
				store_function(function, 6, store_count);
			}
			break;
		case '*':
			m_expecting_arg = true;
			store_function(StkMul, 3, store_count);
			break;
		case '/':
			m_expecting_arg = true;
			store_function(StkDiv, 3, store_count);
			break;
		case '^':
			m_expecting_arg = true;
			store_function(StkPwr, 2, store_count);
			break;
		case '=':
			m_expecting_arg = true;
			if (text[n + 1] == '=')
			{
				n++;
				store_function(StkEQ, 6, store_count);
			}
			else
			{
				s_ops[m_posp-1].function = StkSto;
				s_ops[m_posp-1].prec = get_prec(5, store_count);
				m_store[m_store_ptr++] = m_load[--m_load_ptr];
				store_count++;
			}
			break;
		default:
			while (isalnum(text[n + 1]) || text[n + 1] == '.' || text[n + 1] == '_')
			{
				n++;
			}
			int length = (n + 1) - m_initial_n;
			m_expecting_arg = false;
			JumpType jump_type = is_jump_keyword(&text[m_initial_n], length);
			if (jump_type != JUMPTYPE_NONE)
			{
				m_uses_jump = true;
				switch (jump_type)
				{
				case JUMPTYPE_IF:
					m_expecting_arg = true;
					m_jump_control[m_jump_index++].type = JUMPTYPE_IF;
					store_function(StkJumpOnFalse, 1);
					break;
				case JUMPTYPE_ELSEIF:
					m_expecting_arg = true;
					m_jump_control[m_jump_index++].type = JUMPTYPE_ELSEIF;
					m_jump_control[m_jump_index++].type = JUMPTYPE_ELSEIF;
					store_function(StkJump, 1);
					store_function(0, 15);
					store_function(StkClr, -30000);
					store_function(StkJumpOnFalse, 1);
					break;
				case JUMPTYPE_ELSE:
					m_jump_control[m_jump_index++].type = JUMPTYPE_ELSE;
					store_function(StkJump, 1);
					break;
				case JUMPTYPE_ENDIF:
					m_jump_control[m_jump_index++].type = JUMPTYPE_ENDIF;
					store_function(StkJumpLabel, 1);
					break;
				default:
					assert(false && "Bad jump type");
					break;
				}
			}
			else
			{
				s_ops[m_posp].function = is_function(&text[m_initial_n], length);
				if (s_ops[m_posp].function != not_a_function)
				{
					s_ops[m_posp++].prec = get_prec(1, store_count);
					m_expecting_arg = true;
				}
				else
				{
					ConstArg *c = is_constant(&text[m_initial_n], length);
					m_load[m_load_ptr++] = &(c->argument);
					store_function(StkLod, 1, store_count);
					n = m_initial_n + c->name_length - 1;
				}
			}
			break;
		}
	}
	store_function(0, 16);
	m_next_operation = 0;
	m_last_op = m_posp;
	while (m_next_operation < m_posp)
	{
		if (s_ops[m_next_operation].function)
		{
			sort_prec();
		}
		else
		{
			m_next_operation++;
			m_last_op--;
		}
	}
	return false;
}

int Formula::orbit()
{
	if (!formula_defined() || g_overflow)
	{
		return 1;
	}

	m_load_ptr = m_initial_load_pointer;
	m_store_ptr = m_initial_store_pointer;
	m_op_index = m_initial_op_pointer;
	m_jump_index = m_initial_jump_index;
	// Set the random number
	if (s_random.random() || s_random.randomized())
	{
		switch (m_math_type)
		{
		case FLOATING_POINT_MATH:
			dRandom();
			break;
#if !defined(NO_FIXED_POINT_MATH)
		case FIXED_POINT_MATH:
			lRandom();
			break;
#endif
		}
	}

	g_argument1 = &m_argument_stack[0];
	g_argument2 = g_argument1-1;
	while (m_op_index < m_last_op)
	{
		m_functions[m_op_index]();
		m_op_index++;
	}

	switch (m_math_type)
	{
	case FLOATING_POINT_MATH:
		g_old_z = g_new_z = m_variables[VARIABLE_Z].argument.d;
		return g_argument1->d.real() == 0.0;
#if !defined(NO_FIXED_POINT_MATH)
	case FIXED_POINT_MATH:
		g_old_z_l = g_new_z_l = m_variables[VARIABLE_Z].argument.l;
		if (g_overflow)
		{
			return 1;
		}
		return g_argument1->l.real() == 0L;
#endif
	}
	return 1;
}

int Formula::per_pixel()
{
#if defined(_WIN32)
	_ASSERTE(_CrtCheckMemory());
#endif
	if (!formula_defined())
	{
		return 1;
	}
	g_overflow = false;
	m_load_ptr = 0;
	m_store_ptr = 0;
	m_op_index = 0;
	m_jump_index = 0;
	g_argument1 = &m_argument_stack[0];
	g_argument2 = g_argument1;
	g_argument2--;


	m_variables[VARIABLE_SCRN_PIX].argument.d.real(double(g_col));
	m_variables[VARIABLE_SCRN_PIX].argument.d.imag(double(g_row));

	switch (m_math_type)
	{
	case FLOATING_POINT_MATH:
		m_variables[VARIABLE_WHITE_SQ].argument.d.real(((g_row + g_col) & 1) ? 1.0 : 0.0);
		m_variables[VARIABLE_WHITE_SQ].argument.d.imag(0.0);
		break;


#if !defined(NO_FIXED_POINT_MATH)
	case FIXED_POINT_MATH:
		m_variables[VARIABLE_WHITE_SQ].argument.l.real(DoubleToFudge((g_row + g_col) & 1));
		m_variables[VARIABLE_WHITE_SQ].argument.l.imag(0L);
		m_variables[VARIABLE_SCRN_PIX].argument.l.real(g_col);
		m_variables[VARIABLE_SCRN_PIX].argument.l.real(m_variables[VARIABLE_SCRN_PIX].argument.l.real() << g_bit_shift);
		m_variables[VARIABLE_SCRN_PIX].argument.l.imag(g_row);
		m_variables[VARIABLE_SCRN_PIX].argument.l.imag(m_variables[VARIABLE_SCRN_PIX].argument.l.imag() << g_bit_shift);
		break;
#endif
	}

	// TW started additions for inversion support here 4/17/94
	if (g_invert)
	{
		invert_z(&g_old_z);
		switch (m_math_type)
		{
		case FLOATING_POINT_MATH:
			m_variables[VARIABLE_PIXEL].argument.d.real(g_old_z.real());
			m_variables[VARIABLE_PIXEL].argument.d.imag(g_old_z.imag());
			break;
#if !defined(NO_FIXED_POINT_MATH)
		case FIXED_POINT_MATH:
			// watch out for overflow
			if (norm(g_old_z) >= 127)
			{
				g_old_z.real(8);  // value to bail out in one iteration
				g_old_z.imag(8);
			}
			// convert to fudged longs
			m_variables[VARIABLE_PIXEL].argument.l = ComplexDoubleToFudge(g_old_z);
			break;
#endif
		}
	}
	else
	{
		// TW end of inversion support changes here 4/17/94
		switch (m_math_type)
		{
		case FLOATING_POINT_MATH:
			m_variables[VARIABLE_PIXEL].argument.d = g_externs.DPixel();
			break;
#if !defined(NO_FIXED_POINT_MATH)
		case FIXED_POINT_MATH:
			m_variables[VARIABLE_PIXEL].argument.l = g_externs.LPixel();
			break;
#endif
		}
	}

	if (m_last_init_op)
	{
		m_last_init_op = m_last_op;
	}
	while (m_op_index < m_last_init_op)
	{
		m_functions[m_op_index]();
		m_op_index++;
	}
	m_initial_load_pointer = m_load_ptr;
	m_initial_store_pointer = m_store_ptr;
	m_initial_op_pointer = m_op_index;
	// Set old variable for orbits
	switch (m_math_type)
	{
	case FLOATING_POINT_MATH:
		g_old_z = m_variables[VARIABLE_Z].argument.d;
		break;
#if !defined(NO_FIXED_POINT_MATH)
	case FIXED_POINT_MATH:
		g_old_z_l = m_variables[VARIABLE_Z].argument.l;
		break;
#endif
	}

	return g_overflow ? 0 : 1;
}

int Formula::fill_if_group(int endif_index, JUMP_PTRS *jump_data)
{
	int i = endif_index;
	int last_jump_processed = endif_index;
	while (i > 0)
	{
		i--;
		switch (m_jump_control[i].type)
		{
		case JUMPTYPE_IF:    /*if (); this concludes processing of this group*/
			m_jump_control[i].ptrs = jump_data[last_jump_processed];
			m_jump_control[i].DestJumpIndex = last_jump_processed + 1;
			return i;

		case JUMPTYPE_ELSEIF:    /*elseif* (2 jumps, the else and the if*/
			// first, the "if" part
			m_jump_control[i].ptrs = jump_data[last_jump_processed];
			m_jump_control[i].DestJumpIndex = last_jump_processed + 1;

			// then, the else part
			i--; /*fall through to "else" is intentional*/

		case JUMPTYPE_ELSE:
			m_jump_control[i].ptrs = jump_data[endif_index];
			m_jump_control[i].DestJumpIndex = endif_index + 1;
			last_jump_processed = i;
			break;

		case JUMPTYPE_ENDIF:
			i = fill_if_group(i, jump_data);
			break;

		default:
			break;
		}
	}
	assert(!"fill_if_group should never get here");
	return -1;
}

bool Formula::fill_jump_struct()
{
	// Completes all entries in jump structure. Returns 1 on error)
	// On entry, m_jump_index is the number of jump functions in the formula
	int i = 0;
	int load_count = 0;
	int store_count = 0;
	bool check_for_else = false;
	void (*JumpFunc)() = 0;
	bool find_new_func = true;

	JUMP_PTRS jump_data[MAX_JUMPS];

	for (m_op_index = 0; m_op_index < m_last_op; m_op_index++)
	{
		if (find_new_func)
		{
			switch (m_jump_control[i].type)
			{
			case JUMPTYPE_IF:
				JumpFunc = StkJumpOnFalse;
				break;
			case JUMPTYPE_ELSEIF:
				check_for_else = !check_for_else;
				JumpFunc = check_for_else ? StkJump : StkJumpOnFalse;
				break;
			case JUMPTYPE_ELSE:
				JumpFunc = StkJump;
				break;
			case JUMPTYPE_ENDIF:
				JumpFunc = StkJumpLabel;
				break;
			default:
				break;
			}
			find_new_func = false;
		}
		if (*(m_functions[m_op_index]) == StkLod)
		{
			load_count++;
		}
		else if (*(m_functions[m_op_index]) == StkSto)
		{
			store_count++;
		}
		else if (*(m_functions[m_op_index]) == JumpFunc)
		{
			jump_data[i].JumpOpPtr = m_op_index;
			jump_data[i].JumpLodPtr = load_count;
			jump_data[i].JumpStoPtr = store_count;
			i++;
			find_new_func = true;
		}
	}

	// Following for safety only; all should always be false
	if (i != m_jump_index || m_jump_control[i - 1].type != JUMPTYPE_ENDIF
		|| m_jump_control[0].type != JUMPTYPE_IF)
	{
		return true;
	}

	while (i > 0)
	{
		i = fill_if_group(i-1, jump_data);
	}
	return (i < 0);
}

static int formula_get_char(std::ifstream &openfile)
{
	int c;
	bool done = false;
	bool line_wrap = false;
	while (!done)
	{
		c = openfile.get();
		switch (c)
		{
		case '\r':
		case ' ' :
		case '\t' :
			break;
		case '\\':
			line_wrap = true;
			break;
		case ';' :
			do
			{
				c = openfile.get();
			}
			while (c != '\n' && c != EOF && c != CTRL_Z);
			if (c == EOF || c == CTRL_Z)
			{
				done = true;
			}
		case '\n' :
			if (!line_wrap)
			{
				done = true;
			}
			line_wrap = false;
			break;
		default:
			done = true;
			break;
		}
	}
	return tolower(c);
}

// This function also gets flow control info

static bool get_function_information(FormulaToken *token)
{
	for (int i = 0; i < NUM_OF(s_function_list); i++)
	{
		if (!strcmp(s_function_list[i].name, token->text))
		{
			token->id = i;
			token->type = s_function_list[i].tokenType;
			return true;
		}
	}
	return false;
}
static bool get_flow_control_information(FormulaToken *token)
{
	for (int i = 0; i < NUM_OF(s_jump_list); i++)  /*pick up flow control*/
	{
		if (!strcmp(s_jump_list[i].name, token->text))
		{
			token->type = TOKENTYPE_FLOW_CONTROL;
			token->id   = s_jump_list[i].jumpType;
			return true;
		}
	}
	return false;
}
static void get_function_or_flow_control_information(FormulaToken *token)
{
	if (!get_function_information(token) && !get_flow_control_information(token))
	{
		token->SetError(TOKENID_ERROR_UNDEFINED_FUNCTION);
	}
}

static void get_variable_information(FormulaToken *token)
{
	for (int i = 0; i < NUM_OF(s_constants); i++)
	{
		if (!strcmp(s_constants[i].name, token->text))
		{
			token->id = s_constants[i].variable;
			token->type = s_constants[i].tokenType;
			return;
		}
	}
	token->type = TOKENTYPE_USER_VARIABLE;
	token->id = 0;
}

// fills in token structure where numeric constant is indicated
/* Note - this function will be called twice to fill in the components
		of a complex constant. See is_complex_constant() below. */
// returns true on success, false on TOKENTYPE_NONE
static bool formula_get_constant(std::ifstream &openfile, FormulaToken *token)
{
	int i = 1;
	bool getting_base = true;
	std::ifstream::pos_type filepos = openfile.tellg();
	token->value.real(0.0);          /*initialize values to 0*/
	token->value.imag(0.0);
	bool got_decimal_already = (token->text[0] == '.');
	bool done = false;
	while (!done)
	{
		int c = formula_get_char(openfile);
		switch (c)
		{
		case EOF:
		case CTRL_Z:
			token->text[i] = 0;
			token->SetError(TOKENID_ERROR_END_OF_FILE);
			return false;
		CASE_NUM:
			token->text[i++] = char(c);
			filepos = openfile.tellg();
			break;
		case '.':
			if (got_decimal_already || !getting_base)
			{
				token->text[i++] = char(c);
				token->text[i++] = 0;
				token->SetError(TOKENID_ERROR_ILL_FORMED_CONSTANT);
				return false;
			}
			else
			{
				token->text[i++] = char(c);
				got_decimal_already = true;
				filepos = openfile.tellg();
			}
			break;
		default:
			if (c == 'e'
				&& getting_base
				&& (isdigit(token->text[i-1])
					|| (token->text[i-1] == '.' && i > 1)))
			{
				token->text[i++] = char(c);
				getting_base = false;
				got_decimal_already = false;
				filepos = openfile.tellg();
				c = formula_get_char(openfile);
				if (c == '-' || c == '+')
				{
					token->text[i++] = char(c);
					filepos = openfile.tellg();
				}
				else
				{
					openfile.seekg(filepos, SEEK_SET);
				}
			}
			else if (isalpha(c) || c == '_')
			{
				token->text[i++] = char(c);
				token->text[i++] = 0;
				token->SetError(TOKENID_ERROR_ILL_FORMED_CONSTANT);
				return false;
			}
			else if (token->text[i-1] == 'e' || (token->text[i-1] == '.' && i == 1))
			{
				token->text[i++] = char(c);
				token->text[i++] = 0;
				token->SetError(TOKENID_ERROR_ILL_FORMED_CONSTANT);
				return false;
			}
			else
			{
				openfile.seekg(filepos, SEEK_SET);
				token->text[i++] = 0;
				done = true;
			}
			break;
		}
		if (i == MAX_TOKEN_LENGTH && token->text[MAX_TOKEN_LENGTH-1])
		{
			token->text[MAX_TOKEN_LENGTH] = 0;
			token->SetError(TOKENID_ERROR_TOKEN_TOO_LONG);
			return false;
		}
	}    // end of while loop. Now fill in the value
	token->SetValue(atof(token->text));
	token->id = 0;
	return true;
}

static void append_value(FormulaToken *token, const FormulaToken &temp_tok, int sign_value,
						 const char *suffix)
{
	if (sign_value == -1)
	{
		strcat(token->text, "-");
	}
	strcat(token->text, temp_tok.text);
	strcat(token->text, suffix);
}
static void debug_dump(std::ofstream &stream, const char *message)
{
	if (stream)
	{
		stream << message;
	}
}
static void debug_dump(std::ofstream &stream, const boost::format &message)
{
	if (stream.is_open())
	{
		stream << message;
	}
}
static void is_complex_constant(std::ifstream &openfile, FormulaToken *token)
{
	// should test to make sure token->token_str[0] == '('
	token->text[1] = 0;  // so we can concatenate later

	long filepos = openfile.tellg();
	std::ofstream debug_token;
	if (DEBUGMODE_DISK_MESSAGES == g_debug_mode)
	{
		debug_token.open("frmconst.txt", std::ios_base::out | std::ios_base::ate);
	}

	FormulaToken temp_tok;
	int sign_value = 1;
	bool getting_real = true;
	bool done = false;
	while (!done)
	{
		int c = formula_get_char(openfile);
		switch (c)
		{
		CASE_NUM:
		case '.':
			debug_dump(debug_token, boost::format("Set temp_tok.token_str[0] to %c\n") % c);
			temp_tok.text[0] = char(c);
			break;
		case '-':
			debug_dump(debug_token, "First char is a minus\n");
			sign_value = -1;
			c = formula_get_char(openfile);
			if (c == '.' || isdigit(c))
			{
				debug_dump(debug_token, boost::format("Set temp_tok.token_str[0] to %c\n") % c);
				temp_tok.text[0] = char(c);
			}
			else
			{
				debug_dump(debug_token, "First char not a . or NUM\n");
				done = true;
			}
			break;
		default:
			debug_dump(debug_token, "First char not a . or NUM\n");
			done = true;
			break;
		}
		debug_dump(debug_token, boost::format("Calling frmgetconstant unless done is 1; done is %d\n") % int(done));
		if (!done && formula_get_constant(openfile, &temp_tok))
		{
			c = formula_get_char(openfile);
			debug_dump(debug_token, boost::format("frmgetconstant returned 1; next token is %c\n") % c);
			if (getting_real && c == ',')  /*we have the real part now*/
			{
				append_value(token, temp_tok, sign_value, ",");
				token->value.real(temp_tok.value.real()*sign_value);
				getting_real = false;
				sign_value = 1;
			}
			else if (!getting_real && c == ')')  // we have the complex part
			{
				append_value(token, temp_tok, sign_value, ")");
				token->value.imag(temp_tok.value.real()*sign_value);
				token->type = token->value.imag() ? TOKENTYPE_COMPLEX_CONSTANT : TOKENTYPE_REAL_CONSTANT;
				token->id   = 0;
				debug_dump(debug_token, boost::format("Exiting with type set to %d\n")
					% (token->value.imag() ? TOKENTYPE_COMPLEX_CONSTANT : TOKENTYPE_REAL_CONSTANT));
				return;
			}
			else
			{
				done = true;
			}
		}
		else
		{
			done = true;
		}
	}
	openfile.seekg(filepos, SEEK_SET);
	token->text[1] = 0;
	token->value.imag(0.0);
	token->value.real(0.0);
	token->type = TOKENTYPE_PARENTHESIS;
	token->id = TOKENID_OPEN_PARENS;
	debug_dump(debug_token,  "Exiting with ID set to OPEN_PARENS\n");
}

static bool formula_get_alpha(std::ifstream &openfile, FormulaToken *token)
{
	int i = 1;
	bool variable_name_too_long = false;
	std::ifstream::pos_type last_filepos = openfile.tellg();
	int c = formula_get_char(openfile);
	while (c != EOF && c != CTRL_Z)
	{
		long filepos = openfile.tellg();
		switch (c)
		{
		CASE_ALPHA:
		CASE_NUM:
		case '_':
			token->text[i++] = (i < NUM_OF(token->text)) ? char(c) : 0;
			if (i == MAX_TOKEN_LENGTH+1)
			{
				variable_name_too_long = true;
			}
			last_filepos = filepos;
			break;
		default:
			if (c == '.')  /*illegal character in variable or func name*/
			{
				token->SetError(TOKENID_ERROR_ILLEGAL_VARIABLE_NAME);
				token->text[i++] = '.';
				token->text[i] = 0;
				return false;
			}
			else if (variable_name_too_long)
			{
				token->SetError(TOKENID_ERROR_TOKEN_TOO_LONG);
				token->text[i] = 0;
				openfile.seekg(last_filepos, SEEK_SET);
				return false;
			}
			token->text[i] = 0;
			openfile.seekg(last_filepos, SEEK_SET);
			get_function_or_flow_control_information(token);
			if (c == '(')  /*getfuncinfo() correctly filled structure*/
			{
				if (token->type == TOKENTYPE_ERROR)
				{
					return false;
				}
				else if (token->type == TOKENTYPE_FLOW_CONTROL
					&& (token->id == JUMPTYPE_ELSE || token->id == JUMPTYPE_ENDIF))
				{
					token->SetError(TOKENID_ERROR_JUMP_WITH_ILLEGAL_CHAR);
					return false;
				}
				return true;
			}
			/*can't use function names as variables*/
			else if (token->type == TOKENTYPE_FUNCTION || token->type == TOKENTYPE_PARAMETER_FUNCTION)
			{
				token->SetError(TOKENID_ERROR_FUNC_USED_AS_VAR);
				return false;
			}
			else if (token->type == TOKENTYPE_FLOW_CONTROL
				&& (token->id == JUMPTYPE_IF || token->id == JUMPTYPE_ELSEIF))
			{
				token->SetError(TOKENID_ERROR_JUMP_MISSING_BOOLEAN);
				return false;
			}
			else if (token->type == TOKENTYPE_FLOW_CONTROL
				&& (token->id == JUMPTYPE_ELSE || token->id == JUMPTYPE_ENDIF))
			{
				if (c == ',' || c == '\n' || c == ':')
				{
					return true;
				}
				else
				{
					token->SetError(TOKENID_ERROR_JUMP_WITH_ILLEGAL_CHAR);
					return false;
				}
			}
			else
			{
				get_variable_information(token);
				return true;
			}
		}
		c = formula_get_char(openfile);
	}
	token->text[0] = 0;
	token->SetError(TOKENID_ERROR_END_OF_FILE);
	return false;
}

static void formula_get_end_of_string(std::ifstream &openfile, FormulaToken *this_token)
{
	long last_filepos = openfile.tellg();

	int c;
	for (c = formula_get_char(openfile); (c == '\n' || c == ',' || c == ':'); c = formula_get_char(openfile))
	{
		if (c == ':')
		{
			this_token->text[0] = ':';
		}
		last_filepos = openfile.tellg();
	}
	if (c == '}')
	{
		this_token->text[0] = '}';
		this_token->type = TOKENTYPE_END_OF_FORMULA;
		this_token->id   = 0;
	}
	else
	{
		openfile.seekg(last_filepos, SEEK_SET);
		if (this_token->text[0] == '\n')
		{
			this_token->text[0] = ',';
		}
	}
}

/* fills token structure; returns true on success and false on
  TOKENTYPE_NONE and TOKENTYPE_END_OF_FORMULA
*/
static bool formula_get_token(std::ifstream &openfile, FormulaToken *this_token)
{
	int c = formula_get_char(openfile);
	int i = 1;
	switch (c)
	{
	CASE_NUM:
	case '.':
		this_token->text[0] = char(c);
		return formula_get_constant(openfile, this_token);
	CASE_ALPHA:
	case '_':
		this_token->text[0] = char(c);
		return formula_get_alpha(openfile, this_token);
	CASE_TERMINATOR:
		this_token->type = TOKENTYPE_OPERATOR; // this may be changed below
		this_token->text[0] = char(c);
		{
			std::ifstream::pos_type filepos = openfile.tellg();
			if (c == '<' || c == '>' || c == '=')
			{
				c = formula_get_char(openfile);
				if (c == '=')
				{
					this_token->text[i++] = char(c);
				}
				else
				{
					openfile.seekg(filepos, SEEK_SET);
				}
			}
			else if (c == '!')
			{
				c = formula_get_char(openfile);
				if (c == '=')
				{
					this_token->text[i++] = char(c);
				}
				else
				{
					openfile.seekg(filepos, SEEK_SET);
					this_token->text[1] = 0;
					this_token->SetError(TOKENID_ERROR_ILLEGAL_OPERATOR);
					return false;
				}
			}
			else if (c == '|')
			{
				c = formula_get_char(openfile);
				if (c == '|')
				{
					this_token->text[i++] = char(c);
				}
				else
				{
					openfile.seekg(filepos, SEEK_SET);
				}
			}
			else if (c == '&')
			{
				c = formula_get_char(openfile);
				if (c == '&')
				{
					this_token->text[i++] = char(c);
				}
				else
				{
					openfile.seekg(filepos, SEEK_SET);
					this_token->text[1] = 0;
					this_token->SetError(TOKENID_ERROR_ILLEGAL_OPERATOR);
					return false;
				}
			}
			else if (this_token->text[0] == '}')
			{
				this_token->type = TOKENTYPE_END_OF_FORMULA;
				this_token->id   = 0;
			}
			else if (this_token->text[0] == '\n'
				|| this_token->text[0] == ','
				|| this_token->text[0] == ':')
			{
				formula_get_end_of_string(openfile, this_token);
			}
			else if (this_token->text[0] == ')')
			{
				this_token->type = TOKENTYPE_PARENTHESIS;
				this_token->id = TOKENID_CLOSE_PARENS;
			}
			else if (this_token->text[0] == '(')
			{
				/* the following function will set token_type to TOKENTYPE_PARENTHESIS and
					token_id to OPEN_PARENS if this is not the start of a
					complex constant */
				is_complex_constant(openfile, this_token);
				return true;
			}
			this_token->text[i] = 0;
			if (this_token->type == TOKENTYPE_OPERATOR)
			{
				for (int n = 0; n < NUM_OF(s_operator_list); n++)
				{
					if (!strcmp(s_operator_list[n].name, this_token->text))
					{
						this_token->id = s_operator_list[n].type;
					}
				}
			}
			return (this_token->text[0] != '}');
		}
	case EOF:
	case CTRL_Z:
		this_token->text[0] = 0;
		this_token->SetError(TOKENID_ERROR_END_OF_FILE);
		return false;
	default:
		this_token->text[0] = char(c);
		this_token->text[1] = 0;
		this_token->SetError(TOKENID_ERROR_ILLEGAL_CHARACTER);
		return false;
	}
}

void Formula::get_parameter(const char *name)
{
	m_uses_p1 = false;
	m_uses_p2 = false;
	m_uses_p3 = false;
	m_uses_p4 = false;
	m_uses_p5 = false;
	m_uses_is_mand = false;
	m_max_function_number = 0;

	if (!formula_defined())
	{
		return;  // and don't reset the pointers
	}

	std::ifstream entry_file;
	if (!find_file_item(m_filename, name, entry_file, ITEMTYPE_FORMULA))
	{
		stop_message(STOPMSG_NORMAL, error_messages(PE_COULD_NOT_OPEN_FILE_WHERE_FORMULA_LOCATED));
		return;
	}

	{
		int c;
		do
		{
			c = formula_get_char(entry_file);
		}
		while (c != '{' && c != EOF && c != CTRL_Z);
		if (c != '{')
		{
			stop_message(STOPMSG_NORMAL, error_messages(PE_UNEXPECTED_EOF));
			entry_file.close();
			return;
		}
	}

	std::ofstream debug_token;
	if (DEBUGMODE_DISK_MESSAGES == g_debug_mode)
	{
		debug_token.open("frmtokens.txt", std::ios_base::out | std::ios_base::ate);
		debug_dump(debug_token, name);
	}
	FormulaToken current_token;
	while (formula_get_token(entry_file, &current_token))
	{
		if (debug_token)
		{
			debug_token << current_token.text << "\n"
				<< boost::format("token_type is %d\n") % current_token.type
				<< boost::format("token_id is %d\n") % current_token.id;
			if (current_token.type == TOKENTYPE_REAL_CONSTANT || current_token.type == TOKENTYPE_COMPLEX_CONSTANT)
			{
				debug_token << boost::format("Real value is %f\n") % current_token.value.real()
					<< boost::format("Imag value is %f\n") % current_token.value.imag();
			}
			debug_token << "\n";
		}
		switch (current_token.type)
		{
		case TOKENTYPE_PARAMETER_VARIABLE:
			switch (current_token.id)
			{
			case VARIABLE_P1:		m_uses_p1 = true;		break;
			case VARIABLE_P2:		m_uses_p2 = true;		break;
			case VARIABLE_P3:		m_uses_p3 = true;		break;
			case VARIABLE_P4:		m_uses_p4 = true;		break;
			case VARIABLE_P5:		m_uses_p5 = true;		break;
			case VARIABLE_IS_MAND:	m_uses_is_mand = true;	break;
			}
			break;
		case TOKENTYPE_PARAMETER_FUNCTION:
			if ((current_token.id - 10) > m_max_function_number)
			{
				m_max_function_number = current_token.id - 10;
			}
			break;
		}
	}
	entry_file.close();
	debug_token.close();
	if (current_token.type != TOKENTYPE_END_OF_FORMULA)
	{
		m_uses_p1 = false;
		m_uses_p2 = false;
		m_uses_p3 = false;
		m_uses_p4 = false;
		m_uses_p5 = false;
		m_uses_is_mand = false;
		m_max_function_number = 0;
	}
}

/* check_name_and_symmetry():
	error checking to the open brace on the first line;
	return true on success
	return false if errors are found which should cause
	the formula not to be executed
*/
bool Formula::check_name_and_symmetry(std::ifstream &open_file, bool report_bad_symmetry)
{
	long filepos = open_file.tellg();
	// first, test name
	bool at_end_of_name = false;
	int i = 0;
	int c;
	bool done = false;
	while (!done)
	{
		c = open_file.get();
		switch (c)
		{
		case EOF:
		case CTRL_Z:
			stop_message(STOPMSG_NORMAL, error_messages(PE_UNEXPECTED_EOF));
			return false;
		case '\r':
		case '\n':
			stop_message(STOPMSG_NORMAL, error_messages(PE_NO_LEFT_BRACKET_FIRST_LINE));
			return false;
		case ' ':
		case '\t':
			at_end_of_name = true;
			break;
		case '(':
		case '{':
			done = true;
			break;
		default :
			if (!at_end_of_name)
			{
				i++;
			}
			break;
		}
	}

	if (i > ITEMNAMELEN)
	{
		char msgbuf[100];
		strcpy(msgbuf, error_messages(PE_FORMULA_NAME_TOO_LARGE));
		strcat(msgbuf, ":\n   ");
		open_file.seekg(filepos, SEEK_SET);
		int j;
		int k = int_strlen(error_messages(PE_FORMULA_NAME_TOO_LARGE));
		for (j = 0; j < i && j < 25; j++)
		{
			msgbuf[j + k + 2] = char(open_file.get());
		}
		msgbuf[j + k + 2] = 0;
		stop_message(STOPMSG_FIXED_FONT, msgbuf);
		return false;
	}
	// get symmetry
	g_symmetry = SYMMETRY_NONE;
	if (c == '(')
	{
		char symmetry_buffer[20];
		done = false;
		i = 0;
		while (!done)
		{
			c = open_file.get();
			switch (c)
			{
			case EOF:
			case CTRL_Z:
				stop_message(STOPMSG_NORMAL, error_messages(PE_UNEXPECTED_EOF));
				return false;
			case '\r':
			case '\n':
				stop_message(STOPMSG_FIXED_FONT, error_messages(PE_NO_LEFT_BRACKET_FIRST_LINE));
				return false;
			case '{':
				stop_message(STOPMSG_FIXED_FONT, error_messages(PE_NO_MATCH_RIGHT_PAREN));
				return false;
			case ' ':
			case '\t':
				break;
			case ')':
				done = true;
				break;
			default :
				if (i < NUM_OF(symmetry_buffer)-1)
				{
					symmetry_buffer[i++] = char(toupper(c));
				}
				break;
			}
		}
		symmetry_buffer[i] = 0;
		for (i = 0; s_symmetry_list[i].symmetry[0]; i++)
		{
			if (!stricmp(s_symmetry_list[i].symmetry, symmetry_buffer))
			{
				g_symmetry = s_symmetry_list[i].n;
				break;
			}
		}
		if (s_symmetry_list[i].symmetry[0] == 0 && report_bad_symmetry)
		{
			stop_message(STOPMSG_FIXED_FONT, str(boost::format("%s:\n   %s")
				% error_messages(PE_INVALID_SYM_USING_NOSYM)
				% symmetry_buffer));
		}
	}
	if (c != '{')
	{
		done = false;
		while (!done)
		{
			c = open_file.get();
			switch (c)
			{
			case EOF:
			case CTRL_Z:
				stop_message(STOPMSG_FIXED_FONT, error_messages(PE_UNEXPECTED_EOF));
				return false;
			case '\r':
			case '\n':
				stop_message(STOPMSG_FIXED_FONT, error_messages(PE_NO_LEFT_BRACKET_FIRST_LINE));
				return false;
			case '{':
				done = true;
				break;
			default :
				break;
			}
		}
	}
	return true;
}

class FilePositionTransaction
{
public:
	FilePositionTransaction(std::ifstream &file)
		: m_file(file),
		m_position(file.tellg()),
		m_committed(false)
	{
	}
	~FilePositionTransaction()
	{
		if (!m_committed)
		{
			m_file.seekg(m_position, SEEK_SET);
		}
	}
	void Commit()
	{
		m_committed = true;
	}
private:
	std::ifstream &m_file;
	std::ifstream::pos_type m_position;
	bool m_committed;
};

const char *Formula::PrepareFormula(std::ifstream &file, bool report_bad_symmetry)
{
	/* This function sets the
	symmetry and converts a formula into a string  with no spaces,
	and one comma after each expression except where the ':' is placed
	and except the final expression in the formula. The open file passed
	as an argument is open in "rb" mode and is positioned at the first
	letter of the name of the formula to be prepared. This function
	is called from run_formula() below.
	*/

	FormulaToken temp_tok;
	FilePositionTransaction transaction(file);

	/*Test for a repeat*/
	if (!check_name_and_symmetry(file, report_bad_symmetry))
	{
		return 0;
	}
	if (!prescan(file))
	{
		return 0;
	}

	if (m_chars_in_formula > 8190)
	{
		return 0;
	}

	std::ofstream debug_fp;
	if (DEBUGMODE_DISK_MESSAGES == g_debug_mode)
	{
		debug_fp.open("debugfrm.txt", std::ios_base::out | std::ios_base::ate);
		if (debug_fp.is_open())
		{
			debug_fp << g_formula_state.get_formula() << "\n";
			if (g_symmetry != SYMMETRY_NONE)
			{
				debug_fp << s_symmetry_list[g_symmetry].symmetry << "\n";
			}
		}
	}
	m_prepare_formula_text[0] = 0; // To permit concantenation later

	/*skip opening end-of-lines */
	bool done = false;
	while (!done)
	{
		formula_get_token(file, &temp_tok);
		if (temp_tok.type == TOKENTYPE_ERROR)
		{
			stop_message(STOPMSG_FIXED_FONT, "Unexpected token error in PrepareFormula\n");
			return 0;
		}
		else if (temp_tok.type == TOKENTYPE_END_OF_FORMULA)
		{
			stop_message(STOPMSG_FIXED_FONT, "Formula has no executable instructions\n");
			return 0;
		}
		if (temp_tok.text[0] != ',')
		{
			strcat(m_prepare_formula_text, temp_tok.text);
			done = true;
		}
	}

	done = false;
	while (!done)
	{
		formula_get_token(file, &temp_tok);
		switch (temp_tok.type)
		{
		case TOKENTYPE_ERROR:
			stop_message(STOPMSG_FIXED_FONT, "Unexpected token error in PrepareFormula\n");
			return 0;
		case TOKENTYPE_END_OF_FORMULA:
			done = true;
			break;
		default:
			strcat(m_prepare_formula_text, temp_tok.text);
			break;
		}
	}

	if (debug_fp && m_prepare_formula_text != 0)
	{
		debug_fp << "   " << m_prepare_formula_text << "\n";
	}
	debug_fp.close();

	transaction.Commit();
	return m_prepare_formula_text;
}

int bad_formula()
{
	// this is called when a formula is bad, instead of calling
	// the normal functions which will produce undefined results
	return 1;
}

bool Formula::run_formula(const char *name, bool report_bad_symmetry)
{
	// first set the pointers so they point to a fn which always returns 1
	// TODO: eliminate writing to g_current_fractal_specific
	g_current_fractal_specific->per_pixel = bad_formula;
	g_current_fractal_specific->orbitcalc = bad_formula;

	if (!formula_defined())
	{
		return true;  // and don't reset the pointers
	}

	// add search for FRM files in directory
	std::ifstream entry_file;
	if (!find_file_item(m_filename, name, entry_file, ITEMTYPE_FORMULA))
	{
		stop_message(STOPMSG_NORMAL, error_messages(PE_COULD_NOT_OPEN_FILE_WHERE_FORMULA_LOCATED));
		return true;
	}

	m_formula_text = PrepareFormula(entry_file, report_bad_symmetry);
	entry_file.close();

	if (!m_formula_text)
	{
		return true;   // error in making string
	}

	// No errors while making string
	allocate();  // ParseStr() will test if this alloc worked
	if (parse_string(m_formula_text, 1))
	{
		return true;   // parse failed, don't change fn pointers
	}

	if (m_uses_jump && fill_jump_struct())
	{
		stop_message(STOPMSG_NORMAL, error_messages(PE_ERROR_IN_PARSING_JUMP_STATEMENTS));
		return true;
	}

	// all parses succeeded so set the pointers back to good functions
	// TODO: eliminate writing to g_current_fractal_specific
	FractalTypeSpecificData *target = g_current_fractal_specific;
	target->per_pixel = form_per_pixel;
	target->orbitcalc = formula_orbit;
	return false;
}


bool Formula::setup_fp()
{
	bool result;
	// TODO: when parsera.c contains assembly equivalents, remove !defined(_WIN32)
#if !defined(NO_FIXED_POINT_MATH) && !defined(_WIN32)
	MathType = D_MATH;
	// CAE changed below for fp
	result = !run_formula(g_formula_state.get_formula(), false); // run_formula() returns 1 for failure
	if (result && !(g_orbit_save & ORBITSAVE_SOUND) && !s_random.randomized()
		&& (g_debug_mode != DEBUGMODE_NO_ASM_MANDEL))
	{
		convert_stack(); // run fast assembler code in parsera.asm
		return true;
	}
	return result;
#else
	m_math_type = FLOATING_POINT_MATH;
	result = !run_formula(g_formula_state.get_formula(), false); // run_formula() returns true for failure
#if 0
	if (result && !(g_orbit_save & ORBITSAVE_SOUND) && !s_random.randomized()
		&& (g_debug_mode != DEBUGMODE_NO_ASM_MANDEL))
	{
		convert_stack(); // run fast assembler code in parsera.asm
		return true;
	}
#endif
	return result;
#endif
}

bool Formula::setup_int()
{
#if defined(NO_FIXED_POINT_MATH)
	return integer_unsupported();
#else
	m_math_type = FIXED_POINT_MATH;
	s_fudge = double(1L << g_bit_shift);
	g_fudge_limit = FudgeToDouble(0x7fffffffL);
	s_shift_back = 32 - g_bit_shift;
	return !run_formula(g_formula_state.get_formula(), false);
#endif
}

void Formula::init_misc()
{
	g_argument1 = &m_arg1;
	g_argument2 = &m_arg2; // needed by all the ?Stk* functions
	s_fudge = double(1L << g_bit_shift);
	g_fudge_limit = FudgeToDouble(0x7fffffffL);
	s_shift_back = 32 - g_bit_shift;
	s_delta16 = g_bit_shift - 16;
	g_bit_shift_minus_1 = g_bit_shift-1;
	m_uses_p1 = false;
	m_uses_p2 = false;
	m_uses_p3 = false;
	m_uses_p4 = false;
	m_uses_p5 = false;
	m_uses_is_mand = false;
	m_uses_jump = false;
}


/*
	Allocate sub-arrays from one main malloc, using global variable
	g_type_specific_work_area; calcfrac.c releases this area when calculation
	ends or is terminated.
	Moved the "f" array to be allocated as part of this.
*/

void Formula::allocate()
{
	/* TW Jan 1 1996 Made two passes to determine actual values of
		m_formula_max_ops and m_formula_max_args. */
	for (int pass = 0; pass < 2; pass++)
	{
		free_work_area();
		if (pass == 0)
		{
			m_formula_max_ops = 2300; // this value uses up about 64K memory
			m_formula_max_args = (unsigned) (m_formula_max_ops/2.5);
		}

		m_functions = new t_function_pointer[m_formula_max_ops];
		m_store = new Arg *[MAX_STORES];
		m_load = new Arg *[MAX_LOADS];
		m_variables = new ConstArg[m_formula_max_args];
		m_function_load_store_pointers = new function_load_store[m_formula_max_ops];

		if (pass == 0)
		{
			if (!parse_string(m_formula_text, pass))
			{
				// per Chuck Ebbert, fudge these up a little
				m_formula_max_ops = m_posp + 4;
				m_formula_max_args = m_parser_vsp + 4;
			}
		}
	}
	m_uses_p1 = false;
	m_uses_p2 = false;
	m_uses_p3 = false;
	m_uses_p4 = false;
	m_uses_p5 = false;
#if defined(_WIN32)
	_ASSERTE(_CrtCheckMemory());
#endif
}

void free_work_area()
{
	g_formula_state.free_work_area();
}

template <typename T>
static void delete_array_and_null(T &pointer)
{
	delete[] pointer;
	pointer = 0;
}

void Formula::free_work_area()
{
	delete_array_and_null(m_functions);
	delete_array_and_null(m_store);
	delete_array_and_null(m_load);
	delete_array_and_null(m_variables);
	delete_array_and_null(m_function_load_store_pointers);
}

void Formula::formula_error(std::ifstream &open_file, std::ifstream::pos_type begin_frm)
{
	std::string message = "\n";

	FormulaToken token;
	int token_count;
	int chars_to_error = 0;
	int chars_in_error = 0;
	for (int j = 0; j < NUM_OF(m_errors) && m_errors[j].start_pos; j++)
	{
		int initialization_error = (m_errors[j].error_number == PE_SECOND_COLON) ? 1 : 0;
		open_file.seekg(begin_frm, SEEK_SET);
		int i;
		int line_number = 1;
		while (open_file.tellg() != m_errors[j].error_pos)
		{
			i = open_file.get();
			if (i == '\n')
			{
				line_number++;
			}
			else if (i == EOF || i == '}')
			{
				stop_message(STOPMSG_NORMAL, "Unexpected EOF or end-of-formula in error function.\n");
				open_file.seekg(m_errors[j].error_pos, SEEK_SET);
				formula_get_token(open_file, &token); /*reset file to end of error token */
				return;
			}
		}
		message += str(boost::format("Error(%d) at line %d:  %s\n  ")
			%  m_errors[j].error_number % line_number % error_messages(m_errors[j].error_number));
		i = int(message.length());
		open_file.seekg(m_errors[j].start_pos, SEEK_SET);
		int statement_len = 0;
		token_count = 0;
		bool done = false;
		while (!done)
		{
			long filepos = open_file.tellg();
			if (filepos == m_errors[j].error_pos)
			{
				chars_to_error = statement_len;
				formula_get_token(open_file, &token);
				chars_in_error = int_strlen(token.text);
				statement_len += chars_in_error;
				token_count++;
			}
			else
			{
				formula_get_token(open_file, &token);
				statement_len += int_strlen(token.text);
				token_count++;
			}
			if ((token.type == TOKENTYPE_END_OF_FORMULA)
				|| (token.type == TOKENTYPE_OPERATOR
					&& (token.id == 0 || token.id == 11))
				|| token.IsError(TOKENID_ERROR_END_OF_FILE))
			{
				done = true;
				if (token_count > 1 && !initialization_error)
				{
					token_count--;
				}
			}
		}
		open_file.seekg(m_errors[j].start_pos, SEEK_SET);
		if (chars_in_error < 74)
		{
			while (chars_to_error + chars_in_error > 74)
			{
				formula_get_token(open_file, &token);
				chars_to_error -= int_strlen(token.text);
				token_count--;
			}
		}
		else
		{
			open_file.seekg(m_errors[j].error_pos, SEEK_SET);
			chars_to_error = 0;
			token_count = 1;
		}
		while (message.length() - i <= 74 && token_count--)
		{
			formula_get_token(open_file, &token);
			message += token.text;
		}
		open_file.seekg(m_errors[j].error_pos, SEEK_SET);
		formula_get_token(open_file, &token);
		if (message.length() - i > 74)
		{
			message = message.substr(0, message.length() - i + 74);
		}
		message += "\n";
		i = int(message.length());
		while (chars_to_error-- > -2)
		{
			message += " ";
		}
		if (m_errors[j].error_number == PE_TOKEN_TOO_LONG)
		{
			chars_in_error = 33;
		}
		while (chars_in_error-- && message.length() - i <= 74)
		{
			message += "^";
		}
		message += "\n";
	}
	stop_message(STOPMSG_FIXED_FONT, message);
	return;
}

void Formula::display_var_list() const
{
	if (m_variable_list.size() > 0)
	{
		stop_message(STOPMSG_NORMAL, "List of user defined variables:\n");
		for (std::vector<std::string>::size_type i = 0; i < m_variable_list.size(); i++)
		{
			if (stop_message(STOPMSG_NORMAL, m_variable_list[i]) < 0)
			{
				break;
			}
		}
	}
}

void Formula::display_const_lists() const
{
	if (m_complex_list.size() > 0)
	{
		display_const_list("Complex constants are:", m_complex_list);
	}
	if (m_real_list.size() > 0)
	{
		display_const_list("Real constants are:", m_real_list);
	}
}

void Formula::display_const_list(const char *title, const std::vector<ComplexD> &list) const
{
	stop_message(STOPMSG_NORMAL, title);
	for (std::vector<ComplexD>::size_type i = 0; i < list.size(); i++)
	{
		if (stop_message(STOPMSG_NORMAL, str(boost::format("%f, %f\n") % list[i].real() % list[i].imag())) < 0)
		{
			break;
		}
	}
}

void Formula::display_const_list(const char *title, const std::vector<double> &list) const
{
	stop_message(STOPMSG_NORMAL, title);
	for (std::vector<double>::size_type i = 0; i < list.size(); i++)
	{
		if (stop_message(STOPMSG_NORMAL, str(boost::format("%f\n") % list[i])) < 0)
		{
			break;
		}
	}
}

void Formula::init_var_list()
{
	m_variable_list.clear();
}


void Formula::init_const_lists()
{
	m_complex_list.clear();
	m_real_list.clear();
}

void Formula::count_lists()
{
	m_variable_count = int(m_variable_list.size());
	m_complex_count = int(m_complex_list.size());
	m_real_count = int(m_real_list.size());
#if !defined(NDEBUG)
	display_var_list();
	display_const_lists();
#endif
}



/*Formula::prescan() takes an open file with the file pointer positioned at
the beginning of the relevant formula, and parses the formula, token
by token, for syntax errors. The function also accumulates data for
memory allocation to be done later.

The function returns 1 if success, and 0 if errors are found.
*/

void Formula::record_error(int error_code)
{
	if (!m_errors_found || m_errors[m_errors_found - 1].start_pos != m_statement_pos)
	{
		m_errors[m_errors_found].start_pos      = m_statement_pos;
		m_errors[m_errors_found].error_pos      = m_file_pos;
		m_errors[m_errors_found++].error_number = error_code;
	}
}

template <typename T>
void add_new(std::vector<T> &list, const T &value)
{
	if (list.end() == std::find(list.begin(), list.end(), value))
	{
		list.push_back(value);
	}
}

bool Formula::prescan(std::ifstream &open_file)
{
	m_errors_found = 0;
	bool expecting_argument = true;
	bool new_statement = true;
	bool assignment_ok = true;
	bool already_got_colon = false;
	unsigned long else_has_been_used = 0;
	unsigned long waiting_for_mod = 0;
	int waiting_for_endif = 0;
	int max_parens = sizeof(m_parenthesis_count)*BITS_PER_BYTE;

	m_number_of_ops = 0;
	m_number_of_loads = 0;
	m_number_of_stores = 0;
	m_number_of_jumps = 0;
	m_chars_in_formula = (unsigned) 0;
	m_uses_jump = false;
	m_parenthesis_count = 0;

	init_var_list();
	init_const_lists();

	std::ifstream::pos_type orig_pos = open_file.tellg();
	m_statement_pos = orig_pos;

	for (int i = 0; i < NUM_OF(m_errors); i++)
	{
		m_errors[i].start_pos    = 0L;
		m_errors[i].error_pos    = 0L;
		m_errors[i].error_number = 0;
	}

	FormulaToken this_token;
	bool done = false;
	while (!done)
	{
		m_file_pos = open_file.tellg();
		formula_get_token(open_file, &this_token);
		m_chars_in_formula += int_strlen(this_token.text);
		switch (this_token.type)
		{
		case TOKENTYPE_ERROR:
			assignment_ok = false;
			switch (this_token.id)
			{
			case TOKENID_ERROR_END_OF_FILE:
				stop_message(STOPMSG_NORMAL, error_messages(PE_UNEXPECTED_EOF));
				open_file.seekg(orig_pos, SEEK_SET);
				return false;
			case TOKENID_ERROR_ILLEGAL_CHARACTER:
				record_error(PE_ILLEGAL_CHAR);
				break;
			case TOKENID_ERROR_ILLEGAL_VARIABLE_NAME:
				record_error(PE_ILLEGAL_VAR_NAME);
				break;
			case TOKENID_ERROR_TOKEN_TOO_LONG:
				record_error(PE_TOKEN_TOO_LONG);
				break;
			case TOKENID_ERROR_FUNC_USED_AS_VAR:
				record_error(PE_FUNC_USED_AS_VAR);
				break;
			case TOKENID_ERROR_JUMP_MISSING_BOOLEAN:
				record_error(PE_JUMP_NEEDS_BOOLEAN);
				break;
			case TOKENID_ERROR_JUMP_WITH_ILLEGAL_CHAR:
				record_error(PE_NO_CHAR_AFTER_THIS_JUMP);
				break;
			case TOKENID_ERROR_UNDEFINED_FUNCTION:
				record_error(PE_UNDEFINED_FUNCTION);
				break;
			case TOKENID_ERROR_ILLEGAL_OPERATOR:
				record_error(PE_UNDEFINED_OPERATOR);
				break;
			case TOKENID_ERROR_ILL_FORMED_CONSTANT:
				record_error(PE_INVALID_CONST);
				break;
			default:
				stop_message(STOPMSG_NORMAL, "Unexpected arrival at default case in prescan()");
				open_file.seekg(orig_pos, SEEK_SET);
				return false;
			}
			break;
		case TOKENTYPE_PARENTHESIS:
			assignment_ok = false;
			new_statement = false;
			switch (this_token.id)
			{
			case TOKENID_OPEN_PARENS:
				if (++m_parenthesis_count > max_parens)
				{
					record_error(PE_NESTING_TOO_DEEP);
				}
				else if (!expecting_argument)
				{
					record_error(PE_SHOULD_BE_OPERATOR);
				}
				waiting_for_mod <<= 1;
				break;
			case TOKENID_CLOSE_PARENS:
				if (m_parenthesis_count)
				{
					m_parenthesis_count--;
				}
				else
				{
					record_error(PE_NEED_A_MATCHING_OPEN_PARENS);
					m_parenthesis_count = 0;
				}
				if (waiting_for_mod & 1L)
				{
					record_error(PE_UNMATCHED_MODULUS);
				}
				else
				{
					waiting_for_mod >>= 1;
				}
				if (expecting_argument)
				{
					record_error(PE_SHOULD_BE_ARGUMENT);
				}
				break;
			default:
				break;
			}
			break;
		case TOKENTYPE_PARAMETER_VARIABLE: /*i.e. p1, p2, p3, p4 or p5*/
			m_number_of_ops++;
			m_number_of_loads++;
			new_statement = false;
			if (!expecting_argument)
			{
				record_error(PE_SHOULD_BE_OPERATOR);
			}
			expecting_argument = false;
			break;
		case TOKENTYPE_USER_VARIABLE: // i.e. c, iter, etc.
			m_number_of_ops++;
			m_number_of_loads++;
			new_statement = false;
			if (!expecting_argument)
			{
				record_error(PE_SHOULD_BE_OPERATOR);
			}
			expecting_argument = false;
			add_new(m_variable_list, std::string(this_token.text));
			// TODO: handle container out of memory
			//if (m_variable_list == 0)
			//{
			//	stop_message(STOPMSG_NORMAL, error_messages(PE_INSUFFICIENT_MEM_FOR_TYPE_FORMULA));
			//	open_file.seekg(orig_pos, SEEK_SET);
			//	init_var_list();
			//	init_const_lists();
			//	return false;
			//}
			break;
		case TOKENTYPE_PREDEFINED_VARIABLE: // i.e. z, pixel, whitesq, etc.
			m_number_of_ops++;
			m_number_of_loads++;
			new_statement = false;
			if (!expecting_argument)
			{
				record_error(PE_SHOULD_BE_OPERATOR);
			}
			expecting_argument = false;
			break;
		case TOKENTYPE_REAL_CONSTANT: // i.e. 4, (4,0), etc.)
			assignment_ok = false;
			m_number_of_ops++;
			m_number_of_loads++;
			new_statement = false;
			if (!expecting_argument)
			{
				record_error(PE_SHOULD_BE_OPERATOR);
			}
			expecting_argument = false;
			add_new(m_real_list, this_token.value.real());
			// TODO: handle out of memory with container
			//if (m_real_list == 0)
			//{
			//	stop_message(STOPMSG_NORMAL, error_messages(PE_INSUFFICIENT_MEM_FOR_TYPE_FORMULA));
			//	open_file.seekg(orig_pos, SEEK_SET);
			//	init_var_list();
			//	init_const_lists();
			//	return false;
			//}
			break;
		case TOKENTYPE_COMPLEX_CONSTANT: // i.e. (1,2) etc.
			assignment_ok = false;
			m_number_of_ops++;
			m_number_of_loads++;
			new_statement = false;
			if (!expecting_argument)
			{
				record_error(PE_SHOULD_BE_OPERATOR);
			}
			expecting_argument = false;
			add_new(m_complex_list, this_token.value);
			// TODO: handle out of memory with container
			//if (m_complex_list == 0)
			//{
			//	stop_message(STOPMSG_NORMAL, error_messages(PE_INSUFFICIENT_MEM_FOR_TYPE_FORMULA));
			//	open_file.seekg(orig_pos, SEEK_SET);
			//	init_var_list();
			//	init_const_lists();
			//	return false;
			//}
			break;
		case TOKENTYPE_FUNCTION:
			assignment_ok = false;
			new_statement = false;
			m_number_of_ops++;
			if (!expecting_argument)
			{
				record_error(PE_SHOULD_BE_OPERATOR);
			}
			break;
		case TOKENTYPE_PARAMETER_FUNCTION:
			assignment_ok = false;
			new_statement = false;
			m_number_of_ops++;
			if (!expecting_argument)
			{
				record_error(PE_SHOULD_BE_OPERATOR);
			}
			new_statement = false;
			break;
		case TOKENTYPE_FLOW_CONTROL:
			assignment_ok = false;
			m_number_of_ops++;
			m_number_of_jumps++;
			if (!new_statement)
			{
				record_error(PE_JUMP_NOT_FIRST);
			}
			else
			{
				m_uses_jump = true;
				switch (this_token.id)
				{
				case JUMPTYPE_IF:
					else_has_been_used <<= 1;
					waiting_for_endif++;
					break;
				case JUMPTYPE_ELSEIF:
					m_number_of_ops += 3; /*else + two clear statements*/
					m_number_of_jumps++;  // this involves two jumps
					if (else_has_been_used & 1)
					{
						record_error(PE_ENDIF_REQUIRED_AFTER_ELSE);
					}
					else if (!waiting_for_endif)
					{
						record_error(PE_MISPLACED_ELSE_OR_ELSEIF);
					}
					break;
				case JUMPTYPE_ELSE:
					if (else_has_been_used & 1)
					{
						record_error(PE_ENDIF_REQUIRED_AFTER_ELSE);
					}
					else if (!waiting_for_endif)
					{
						record_error(PE_MISPLACED_ELSE_OR_ELSEIF);
					}
					else_has_been_used |= 1;
					break;
				case JUMPTYPE_ENDIF:
					else_has_been_used >>= 1;
					waiting_for_endif--;
					if (waiting_for_endif < 0)
					{
						record_error(PE_ENDIF_WITH_NO_IF);
						waiting_for_endif = 0;
					}
					break;
				default:
					break;
				}
			}
			break;
		case TOKENTYPE_OPERATOR:
			m_number_of_ops++; /*This will be corrected below in certain cases*/
			switch (this_token.id)
			{
			case OPERATOR_COMMA:
			case OPERATOR_COLON:
				m_number_of_ops++; // ParseStr inserts a dummy op
				if (m_parenthesis_count)
				{
					record_error(PE_NEED_MORE_CLOSE_PARENS);
					m_parenthesis_count = 0;
				}
				if (waiting_for_mod)
				{
					record_error(PE_UNMATCHED_MODULUS);
					waiting_for_mod = 0;
				}
				if (!expecting_argument)
				{
					if (this_token.id == 11)
					{
						m_number_of_ops += 2;
					}
					else
					{
						m_number_of_ops++;
					}
				}
				else if (!new_statement)
				{
					record_error(PE_SHOULD_BE_ARGUMENT);
				}
				if (this_token.id == 11 && waiting_for_endif)
				{
					record_error(PE_UNMATCHED_IF_IN_INIT_SECTION);
					waiting_for_endif = 0;
				}
				if (this_token.id == 11 && already_got_colon)
				{
					record_error(PE_SECOND_COLON);
				}
				if (this_token.id == 11)
				{
					already_got_colon = true;
				}
				new_statement = true;
				assignment_ok = true;
				expecting_argument = true;
				m_statement_pos = open_file.tellg();
				break;
			case OPERATOR_NOT_EQUAL:
				assignment_ok = false;
				if (expecting_argument)
				{
					record_error(PE_SHOULD_BE_ARGUMENT);
				}
				expecting_argument = true;
				break;
			case OPERATOR_ASSIGNMENT:
				m_number_of_ops--; /*this just converts a load to a store*/
				m_number_of_loads--;
				m_number_of_stores++;
				if (!assignment_ok)
				{
					record_error(PE_ILLEGAL_ASSIGNMENT);
				}
				expecting_argument = true;
				break;
			case OPERATOR_EQUAL:
				assignment_ok = false;
				if (expecting_argument)
				{
					record_error(PE_SHOULD_BE_ARGUMENT);
				}
				expecting_argument = true;
				break;
			case OPERATOR_LESS:
				assignment_ok = false;
				if (expecting_argument)
				{
					record_error(PE_SHOULD_BE_ARGUMENT);
				}
				expecting_argument = true;
				break;
			case OPERATOR_LESS_EQUAL:
				assignment_ok = false;
				if (expecting_argument)
				{
					record_error(PE_SHOULD_BE_ARGUMENT);
				}
				expecting_argument = true;
				break;
			case OPERATOR_GREATER:
				assignment_ok = false;
				if (expecting_argument)
				{
					record_error(PE_SHOULD_BE_ARGUMENT);
				}
				expecting_argument = true;
				break;
			case OPERATOR_GREATER_EQUAL:
				assignment_ok = false;
				if (expecting_argument)
				{
					record_error(PE_SHOULD_BE_ARGUMENT);
				}
				expecting_argument = true;
				break;
			case OPERATOR_MODULUS:
				assignment_ok = false;
				if (!waiting_for_mod & 1L)
				{
					m_number_of_ops--;
				}
				if (!(waiting_for_mod & 1L) && !expecting_argument)
				{
					record_error(PE_SHOULD_BE_OPERATOR);
				}
				else if ((waiting_for_mod & 1L) && expecting_argument)
				{
					record_error(PE_SHOULD_BE_ARGUMENT);
				}
				waiting_for_mod ^= 1L; /*switch right bit*/
				break;
			case OPERATOR_OR:
				assignment_ok = false;
				if (expecting_argument)
				{
					record_error(PE_SHOULD_BE_ARGUMENT);
				}
				expecting_argument = true;
				break;
			case OPERATOR_AND:
				assignment_ok = false;
				if (expecting_argument)
				{
					record_error(PE_SHOULD_BE_ARGUMENT);
				}
				expecting_argument = true;
				break;
			case OPERATOR_PLUS:
				assignment_ok = false;
				if (expecting_argument)
				{
					record_error(PE_SHOULD_BE_ARGUMENT);
				}
				expecting_argument = true;
				break;
			case OPERATOR_MINUS:
				assignment_ok = false;
				expecting_argument = true;
				break;
			case OPERATOR_MULTIPLY:
				assignment_ok = false;
				if (expecting_argument)
				{
					record_error(PE_SHOULD_BE_ARGUMENT);
				}
				expecting_argument = true;
				break;
			case OPERATOR_DIVIDE:
				assignment_ok = false;
				if (expecting_argument)
				{
					record_error(PE_SHOULD_BE_ARGUMENT);
				}
				expecting_argument = true;
				break;
			case OPERATOR_RAISE_POWER:
				assignment_ok = false;
				if (expecting_argument)
				{
					record_error(PE_SHOULD_BE_ARGUMENT);
				}
				m_file_pos = open_file.tellg();
				formula_get_token (open_file, &this_token);
				if (this_token.text[0] == '-')
				{
					record_error(PE_NO_NEG_AFTER_EXPONENT);
				}
				else
				{
					open_file.seekg(m_file_pos, SEEK_SET);
				}
				expecting_argument = true;
				break;
			default:
				break;
			}
			break;
		case TOKENTYPE_END_OF_FORMULA:
			m_number_of_ops += 3; // Just need one, but a couple of extra just for the heck of it
			if (m_parenthesis_count)
			{
				record_error(PE_NEED_MORE_CLOSE_PARENS);
				m_parenthesis_count = 0;
			}
			if (waiting_for_mod)
			{
				record_error(PE_UNMATCHED_MODULUS);
				waiting_for_mod = 0;
			}
			if (waiting_for_endif)
			{
				record_error(PE_IF_WITH_NO_ENDIF);
				waiting_for_endif = 0;
			}
			if (expecting_argument && !new_statement)
			{
				record_error(PE_SHOULD_BE_ARGUMENT);
				m_statement_pos = open_file.tellg();
			}

			if (m_number_of_jumps >= MAX_JUMPS)
			{
				record_error(PE_TOO_MANY_JUMPS);
			}
			done = true;
			break;

		default:
			break;
		}
		if (m_errors_found == NUM_OF(m_errors))
		{
			done = true;
		}
	}
	if (m_errors[0].start_pos)
	{
		formula_error(open_file, orig_pos);
		open_file.seekg(orig_pos, SEEK_SET);
		return false;
	}
	open_file.seekg(orig_pos, SEEK_SET);

	count_lists();

	return true;
}

const char *Formula::info_line1() const
{
	static char buffer[80];
	std::ostringstream text;
	text << " MaxOps (posp) " << m_posp
		<< " MaxArgs (vsp) " << m_parser_vsp
		<< std::ends;
	::strncpy(buffer, text.str().c_str(), 79);
	buffer[79] = 0;
	return &buffer[0];
}

const char *Formula::info_line2() const
{
	static char buffer[80];
	std::ostringstream text;
	text << "   Store ptr " << m_store_ptr
		<< " Loadptr " << m_load_ptr
		<< " MaxOps var " << m_formula_max_ops
		<< " MaxArgs var " << m_formula_max_args
		<< " LastInitOp " << m_last_init_op
		<< std::ends;
	::strncpy(buffer, text.str().c_str(), 79);
	buffer[79] = 0;
	return &buffer[0];
}

// TODO: remove these glue functions when everything has been refactored to objects
void EndInit()
{
	g_formula_state.end_init();
}

int formula_orbit()
{
	return g_formula_state.orbit();
}

int form_per_pixel()
{
	return g_formula_state.per_pixel();
}

bool formula_setup_fp()
{
	return g_formula_state.setup_fp();
}

bool formula_setup_int()
{
	return g_formula_state.setup_int();
}

void dStkLodDup()
{
	g_formula_state.StackLoadDup_d();
}