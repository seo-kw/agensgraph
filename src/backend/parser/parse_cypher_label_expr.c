/*
 * parse_cypher_expr.c
 *	  handle Cypher expressions in parser
 *
 * Copyright (c) 2017 by Bitnine Global, Inc.
 *
 * IDENTIFICATION
 *	  src/backend/parser/parse_cypher_expr.c
 *
 *
 * To store all types that Cypher supports in a single column, almost all
 * expressions expect jsonb values as their arguments and return a jsonb value.
 * Exceptions are comparison operators (=, <>, <, >, <=, >=) and boolean
 * operators (OR, AND, NOT). Comparison operators take jsonb values and return
 * a bool value. And boolean operators take bool values and return a bool
 * value. This is because they use the existing implementation to evaluate
 * themselves.
 * We use SQL NULL instead of 'null'::jsonb. This makes it easy to implement
 * "operations on NULL values return NULL".
 */

#include "postgres.h"

#include "ag_const.h"
#include "catalog/pg_collation.h"
#include "catalog/pg_type.h"
#include "commands/dbcommands.h"
#include "miscadmin.h"
#include "nodes/makefuncs.h"
#include "nodes/nodeFuncs.h"
#include "nodes/subscripting.h"
#include "optimizer/tlist.h"
#include "parser/analyze.h"
#include "parser/parse_clause.h"
#include "parser/parse_coerce.h"
#include "parser/parse_collate.h"
#include "parser/parse_cypher_expr.h"
#include "parser/parse_cypher_label_expr.h"
#include "parser/parse_graph.h"
#include "parser/parse_expr.h"
#include "parser/parse_func.h"
#include "parser/parse_oper.h"
#include "parser/parse_relation.h"
#include "parser/parse_target.h"
#include "parser/parse_type.h"
#include "parser/parse_cypher_utils.h"
#include "utils/lsyscache.h"
#include "utils/builtins.h"
#include "utils/fmgroids.h"
#include "utils/jsonb.h"
#include "optimizer/optimizer.h"



// TODO: move to C
// TODO: expensive and redundant to call multiple times
/**
 * Returns if each label_name in label_expr->label_names is of kind
 * `label_kind`, if it exists.
 *
 * If label_expr is empty, returns true.
 */
bool validate_label_expr_kind(CypherLabelExpr *label_expr, uint32 graph_oid, char label_kind)
{
    ListCell *lc;
	int current_graph_path = get_graph_path_oid();

    foreach (lc, label_expr->label_names)
    {
		char *label_name;
		Oid cached_lab_oid;

        label_name = strVal(lfirst(lc));
		cached_lab_oid = get_labname_laboid(label_name, current_graph_path);

        if ((cached_lab_oid != InvalidOid) 
			 && (get_labid_typeoid(current_graph_path, cached_lab_oid) 
			 	 != (label_kind == LABEL_KIND_VERTEX ? VERTEXOID : EDGEOID)))
        {
            return false;
        }
    }

    return true;
}

// TODO: can be moved to utils?
int string_list_comparator(const ListCell *a, const ListCell *b)
{
    char *str_a = lfirst(a);
    char *str_b = lfirst(b);
    return strcmp(str_a, str_b);
}

char *get_cluster_name(CypherLabelExpr *label_expr)
{
    ListCell *lc;
    bool is_first;

    // TODO: or, defalt, single, and can be a switch?

    /* or */
    Assert(label_expr->type != LABEL_EXPR_TYPE_OR);

    /* default */
    if (LABEL_EXPR_IS_EMPTY(label_expr))
    {
        return label_expr->kind == LABEL_KIND_VERTEX ?
		AG_VERTEX : AG_EDGE;
    }
    /* single */
    if (LABEL_EXPR_LENGTH(label_expr) == 1)
    {
        // TODO: when multi-label implemented, it will be different
        return (char *)strVal(linitial(label_expr->label_names));
    }

    /* and */
    // TODO: implement

    // TODO: can be done during early parsing?
    // list_sort(label_expr->label_names, &string_list_comparator);

    return NULL;
}