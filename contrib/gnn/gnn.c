/*
 * contrib/gnn/gnn.c
 *
*/
#include "postgres.h"


#include "gnn.h"
#include "plpython.h"
#include "executor/spi.h"
#include "fmgr.h"
#include "catalog/pg_proc.h"

#include "catalog/namespace.h"
#include "utils/lsyscache.h"
#include "utils/builtins.h"
#include "parser/parse_func.h"

PG_MODULE_MAGIC;

extern void _PG_init(void);

/* Linkage to functions in plpython module */
#if PY_MAJOR_VERSION >= 3
typedef PyObject *(*PLyUnicode_FromStringAndSize_t) (const char *s, Py_ssize_t size);
static PLyUnicode_FromStringAndSize_t PLyUnicode_FromStringAndSize_p;
#endif

/*
 * Module initialize function: fetch function pointers for cross-module calls.
 */
void
_PG_init(void)
{
	/* Asserts verify that typedefs above match original declarations */
#if PY_MAJOR_VERSION >= 3
	AssertVariableIsOfType(&PLyUnicode_FromStringAndSize, PLyUnicode_FromStringAndSize_t);
	PLyUnicode_FromStringAndSize_p = (PLyUnicode_FromStringAndSize_t)
		load_external_function("$libdir/" PLPYTHON_LIBNAME, "PLyUnicode_FromStringAndSize",
							   true, NULL);
#endif
}

/* These defines must be after the module init function */
#define PLyUnicode_FromStringAndSize PLyUnicode_FromStringAndSize_p

PG_FUNCTION_INFO_V1(get_aggregate_result);

static Oid
get_funcoid(char* name)
{
	Oid func_oid;
	List *func_name_list;
	Oid argtypes[1];
	Datum result;

	argtypes[0] = FLOAT8ARRAYOID;
	func_name_list = list_make1(makeString(pstrdup(name)));
	func_oid = LookupFuncName(func_name_list, 1, argtypes, false);
	elog(WARNING, "funcname: %s oid:%u",name, func_oid);
	return func_oid;
}


Datum
get_aggregate_result(PG_FUNCTION_ARGS)
{
	/* 
	 * 2-demensioned array
	 * ARRAY[ARRAY[float8]] 
	 */
	ArrayType *collected_messages = PG_GETARG_ARRAYTYPE_P(0);
	/* 
	 * User-defined function name 
	 *
	 * input: float8[][]
	 * output: float8[]
	 */
	text *fname = PG_GETARG_TEXT_P(1);
	char *funcname;
	Oid foid;
	Datum result;

	funcname = text_to_cstring(fname);
	foid = get_funcoid(funcname);
	result = OidFunctionCall1(foid, collected_messages);
	PG_RETURN_ARRAYTYPE_P(result);
}



/*
----------------------
-- playground below --
----------------------
*/
Datum
torch_mean(PG_FUNCTION_ARGS)
{
	ArrayType *array = PG_GETARG_ARRAYTYPE_P(0);
	int dimension = PG_GETARG_INT32(1);
	Oid func_oid = get_funcoid("torch_mean_float8");
	Datum result = OidFunctionCall2(func_oid, array, dimension);
	// Datum result = DirectFunctionCall1(func_oid, array, dimension);

	if (dimension == 0)
	{
		Datum resultFloat = DatumGetFloat8(result);
		PG_RETURN_FLOAT8(resultFloat);
	}
	else
	{
		ArrayType *resultArr = DatumGetArrayTypeP(result);
		PG_RETURN_ARRAYTYPE_P(resultArr);
	}
}

Datum
call_torch_1d(PG_FUNCTION_ARGS)
{
	Datum arg;
    Datum result;
	ArrayType *arr;

	ArrayType *input;
	int 	output_dim;
	Oid element_type;
    int16       typlen;
    bool        typbyval;
    char        typalign;
	Datum      *elements;
    bool       *nulls;
    int         num_elements;
	
	int ndim;
	int *dims;
	int *lbs;

	/* TODO: remove just designate extension function */
	Oid funcoid = PG_GETARG_INT32(0); 

	/* input array */
	// input = PG_GETARG_ARRAYTYPE_P(1);

	/* output dimension */
	// output_dim = PG_GETARG_INT32(2);

	/* collect metadata of input */
	element_type = ARR_ELEMTYPE(input);
	ndim = ARR_NDIM(input);
	dims = ARR_DIMS(input);
	lbs  = ARR_LBOUND(input);



	Oid argtype;
	argtype = get_fn_expr_argtype(fcinfo->flinfo, 0);
	switch (argtype)
	{
		case FLOAT8ARRAYOID:
			elog(WARNING,"FLOAT8ARRAY");
			break;
		default:
			break;
	}

	// /* Convert array dimensions into number of elements */
	// int n_elements = ArrayGetNItems(ARR_NDIM(input), ARR_DIMS(input));
	get_typlenbyvalalign(element_type, &typlen, &typbyval, &typalign);

	deconstruct_array(input,
                      element_type,
                      typlen, typbyval, typalign,
                      &elements, &nulls, &num_elements);

	// for (int row = 0; row < dims[0]; row++)
	// {
	// 	for (int col = 0; col < dims[1]; col++)
	// 	{
	// 		int flat_index = row * dims[1] + col;  /* row-major order */
			
	// 		/* flatten array information extraction */
	// 		elog(INFO, "Element (%d,%d): %g",
	// 			row + lbs[0], col + lbs[1],
	// 			DatumGetFloat8(elements[flat_index]));
	// 	}
	// }
		/* TODO: serialize array from input */

	float8 values[6] = {1.1, 2.2, 3.3, 4.4, 5.5, 6.6};
	Datum  elems[6];
	elems[0] = Float8GetDatum(values[0]);
	elems[1] = Float8GetDatum(values[1]);
	elems[2] = Float8GetDatum(values[2]);
	elems[3] = Float8GetDatum(values[3]);
	elems[4] = Float8GetDatum(values[4]);
	elems[5] = Float8GetDatum(values[5]);

	/* dynamic array deconstruct */
	arr = construct_array(
        elems, 6, FLOAT8OID, sizeof(float8), FLOAT8PASSBYVAL, 'd'
    );

	Oid func_oid;
    List *func_name_list;
	Oid argtypes[1];

	argtypes[0] = FLOAT8ARRAYOID;
	func_name_list = list_make1(makeString(pstrdup("torch_mean_1d")));
	func_oid = LookupFuncName(func_name_list, 1, argtypes, true);
    result = OidFunctionCall1(func_oid, arr);

	pfree(elements);
    pfree(nulls);

	PG_RETURN_FLOAT8(result);
}

Datum
sample_call_torch_function_by_spi(PG_FUNCTION_ARGS)
{
	SPITupleTable *tuptable;
	TupleDesc tupdesc;
	int spiRet;
	char* userPythonFuncName = PG_GETARG_CSTRING(0);
	// char* userFuncArgcount = PG_GETARG_UINT32(1);
	// char* userFuncArgcount = PG_GETARG_UINT32(1);
	// char queryBuf[1024];
	// sprintf(queryBuf,"select %s()");

	if (SPI_connect() != SPI_OK_CONNECT)
		elog(ERROR, "SPI_connect failed");

	char queryBuf[1024] = {0};
	sprintf(queryBuf,"select torch_mean(%s)");
	
	spiRet = SPI_execute(queryBuf, true, 0);

	tuptable = SPI_tuptable;
	tupdesc = tuptable->tupdesc;
	for (uint64 i = 0; i < SPI_processed; i++)
	{
		HeapTuple tuple = tuptable->vals[i];
		bool isnull;
		Datum id_datum = SPI_getbinval(tuple, tupdesc, 1, &isnull);
		if (!isnull)
		{
			int id = DatumGetInt32(id_datum);
		}
	}

	SPI_finish();
	return 0;
}

Datum
sample_import_python_module(PG_FUNCTION_ARGS)
{
		
	// PyObject *pName, *pModule, *pFunc;
	// PyObject *pArgs, *pValue;
	// int result;

	// Py_Initialize();

	// // Convert the file name to a Python string
	// pName = PyUnicode_DecodeFSDefault("mymodule");

	// pModule = PyImport_Import(pName);
	// Py_DECREF(pName);

	// if (pModule != NULL)
	// {
	// 	pFunc = PyObject_GetAttrString(pModule, "add");

	// 	if (pFunc && PyCallable_Check(pFunc))
	// 	{
	// 		// Prepare the arguments for the function call
	// 		pArgs = PyTuple_New(2);
	// 		PyTuple_SetItem(pArgs, 0, PyLong_FromLong(3)); // first arg
	// 		PyTuple_SetItem(pArgs, 1, PyLong_FromLong(4)); // second arg

	// 		// Call the Python function with the arguments
	// 		pValue = PyObject_CallObject(pFunc, pArgs);
	// 		Py_DECREF(pArgs);

	// 		if (pValue != NULL)
	// 		{
	// 			// Convert Python int result to C int
	// 			result = (int)PyLong_AsLong(pValue);
	// 			printf("Result of call: %d\n", result);
	// 			Py_DECREF(pValue);
	// 		}
	// 		else
	// 		{
	// 			Py_DECREF(pFunc);
	// 			Py_DECREF(pModule);
	// 			PyErr_Print();
	// 			fprintf(stderr, "Call failed\n");
	// 			return 1;
	// 		}
	// 	}
	// 	else
	// 	{
	// 		if (PyErr_Occurred())
	// 			PyErr_Print();
	// 		fprintf(stderr, "Cannot find function 'add'\n");
	// 	}
	// 	Py_XDECREF(pFunc);
	// 	Py_DECREF(pModule);
	// }
	// else
	// {
	// 	PyErr_Print();
	// 	fprintf(stderr, "Failed to load \"mymodule\"\n");
	// 	return 1;
	// }

	// // Finish the Python Interpreter
	// Py_Finalize();
	return NULL;
}


