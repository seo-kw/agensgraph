/*
 * parse_cypher_label_expr.c
 *	  handle Cypher label expressions in parser
 *
 * Copyright (c) 2025 by SkaiWorldwide, Inc.
 *
 * IDENTIFICATION
 *	  src/backend/parser/parse_cypher_label_expr.c
 *
 *
 *  Abstraction here...
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


/*
 * Returns the first label in label_expr that exists in the cache and is not
 * of type label_kind.
 *
 * This is a helper function to check if all labels in label_expr are valid in
 * kind, during node\edge creation.
 */
char *find_first_invalid_label(CypherLabelExpr *label_expr,
                               char label_expr_kind, Oid graph_oid)
{
    ListCell *lc;

    foreach (lc, label_expr->label_names)
    {
        char *label_name;
		Oid cached_lab_oid;

        label_name = strVal(lfirst(lc));
		cached_lab_oid = get_labname_laboid(label_name, graph_oid);

        if (cached_lab_oid != InvalidOid && 
			 get_labid_typeoid(graph_oid, cached_lab_oid)
			  != (label_expr_kind == LABEL_KIND_VERTEX ? VERTEXOID : EDGEOID))
        {
            return label_name;
        }
    }

    return NULL;
}

// TODO: can be moved to utils?
/*
 * List comparator for String nodes. The ListCells a and b must
 * contain node pointer of type T_String.
 */
int list_string_cmp(const ListCell *a, const ListCell *b)
{
    Node *na = lfirst(a);
    Node *nb = lfirst(b);

    Assert(IsA(na, String));
    Assert(IsA(nb, String));

    return strcmp(strVal(na), strVal(nb));
}

/*
 * Generates table name for label_expr. It does not check if a table exists
 * for that name. The caller must do existence check before using the table.
 * This function is not applicable for LABEL_EXPR_TYPE_OR.
 */
char *label_expr_table_name(CypherLabelExpr *label_expr,
                            char label_expr_kind)
{
    switch (label_expr->kind)
    {
    case LABEL_EXPR_TYPE_EMPTY:
        return label_expr_kind == LABEL_KIND_VERTEX ? AG_VERTEX :
                                                      AG_EDGE;

    case LABEL_EXPR_TYPE_SINGLE:
        return (char *)strVal(linitial(label_expr->label_names));

    case LABEL_EXPR_TYPE_AND:
        // TODO: implement
        ereport(ERROR,
                (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
                 errmsg("label expression type AND is not implemented")));
        return NULL;

    case LABEL_EXPR_TYPE_OR:
        elog(ERROR, "label expression type OR cannot have a table");
        return NULL;

	case LABEL_EXPR_TYPE_NOT:
		// TODO: implement
		elog(ERROR, "label expression type OR cannot have a table");
		return NULL;
		
    default:
        elog(ERROR, "invalid CypherLabelExpr type");
        return NULL;
    }
}

// TODO: whenever a new field is added, update this one.
bool label_expr_are_equal(CypherLabelExpr *le1, CypherLabelExpr *le2)
{
    ListCell *lc1;
    ListCell *lc2;

    if (le1 == le2)
    {
        return true;
    }

    /**
     * If exactly one is null, return false.
     * TODO: should null be allowed?
     */
    if ((le1 == NULL && le2 != NULL) || (le2 == NULL && le1 != NULL))
    {
        return false;
    }

    if (le1->type != le2->type)
    {
        return false;
    }

    if (LABEL_EXPR_LENGTH(le1) != LABEL_EXPR_LENGTH(le2))
    {
        return false;
    }

    /*
     * Assuming both lists are sorted and have same length.
     */
    forboth(lc1, le1->label_names, lc2, le2->label_names)
    {
        char *le1_label = strVal(lfirst(lc1));
        char *le2_label = strVal(lfirst(lc2));

        if (strcmp(le1_label, le2_label) != 0)
        {
            return false;
        }
    }

    return true;
}

/*
 * Returns true if there is at least one table to be scanned for this
 * label_expr.
 *
 * This helper function is used in the MATCH clause transformation.
 */
bool label_expr_has_tables(CypherLabelExpr *label_expr, char label_expr_kind,
                           Oid graph_oid)
{
    char *table_name;
	Oid cached_lab_oid;
	Oid cached_lab_type_oid;
    ListCell *lc;

    switch (label_expr->kind)
    {
    case LABEL_EXPR_TYPE_EMPTY:
        return true;
    case LABEL_EXPR_TYPE_SINGLE:
    case LABEL_EXPR_TYPE_AND:
        table_name = label_expr_table_name(label_expr, label_expr_kind);
		cached_lab_oid = get_labname_laboid(table_name, graph_oid);
        return (cached_lab_oid != InvalidOid && 
			 get_labid_typeoid(graph_oid, cached_lab_oid)
			  != (label_expr_kind == LABEL_KIND_VERTEX ? VERTEXOID : EDGEOID));
    case LABEL_EXPR_TYPE_OR:
		foreach (lc, label_expr->label_names)
		{
			table_name = strVal(lfirst(lc));
			cached_lab_oid = get_labname_laboid(table_name, graph_oid);
			cached_lab_type_oid = get_labid_typeoid(graph_oid, cached_lab_oid);

            if  (cached_lab_oid != InvalidOid 
				&& cached_lab_type_oid == (label_expr_kind == LABEL_KIND_VERTEX ? VERTEXOID : EDGEOID))
            {
                return true;
            }
		}
		return false;
	case LABEL_EXPR_TYPE_NOT:
        // TODO: implement
        ereport(ERROR,
                (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
                 errmsg("label expression type NOT is not implemented")));
        return false;
    default:
        elog(ERROR, "invalid CypherLabelExpr type");
        return false;
    }
}


char *
getFirstCypherLabelName(Node *n)
{
	char *label_name = NULL;
	
	if (n == NULL)
		return NULL;

	if(IsA(n, CypherNode))
	{
		CypherNode *vertex;
		vertex = n;

		if (vertex->label_expr == NIL)
			return NULL;

		label_name = !LABEL_EXPR_IS_EMPTY(vertex->label_expr) ?
			(char *)strVal(linitial(vertex->label_expr->label_names)) :
			"";
	}
	else if (IsA(n, CypherRel))
	{
		CypherRel *edge;
		edge = n;

		if (edge->label_expr == NIL)
			return NULL;

		label_name = !LABEL_EXPR_IS_EMPTY(edge->label_expr) ?
			(char *)strVal(linitial(edge->label_expr->label_names)) :
			"";
	}
	else
	{
		AssertArg(IsA(n, CypherNode) || IsA(n, CypherRel));
	}

	return label_name;
}

/* TODO: implementation */
int
getFirstCypherLabelLoc(Node *n)
{
	return -1;
}