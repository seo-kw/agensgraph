#include "postgres.h"
#include "fmgr.h"
#include "access/heapam.h"
#include "access/genam.h"
#include "access/htup_details.h"
#include "access/table.h"
#include "catalog/pg_type.h"
#include "catalog/namespace.h"
#include "utils/lsyscache.h"
#include "utils/snapmgr.h"
#include "utils/rel.h"
#include "utils/elog.h"
#include "utils/builtins.h"
#include "utils/fmgroids.h"
#include "catalog/indexing.h"

bool debug = true;  // Global debug flag

/* Temp context for holding a specific node's aggregation */
typedef struct AggregateContext
{
    double feature_sum;         /* Sum of feature values */
    int64 neighbor_count;       /* Count of neighbors */
} AggregateContext;

typedef struct gnn_context
{
    char *source_label;   /* Source label */
    char *target_label;   /* Target label */
    char *relationship;   /* Relationship type */
    char *source_pk_col;  /* Source primary key column */
    char *target_pk_col;  /* Target primary key column */
    char *source_fk_col;  /* Source foreign key column */
    char *target_fk_col;  /* Target foreign key column */
    char *feature_name;   /* Feature name to aggregate */

    Relation source_rel;     /* Source relation */
    Relation target_rel;     /* Target relation */
    Relation target_idx;     /* Index on target relation */
    Relation rs_rel;         /* Relationship relation */
    Relation rs_idx;         /* Index on relationship relation */
    CatalogIndexState indstate; /* Index state needed for UPDATES */
} gnn_context;

PG_MODULE_MAGIC;

static void init_gnn_context(gnn_context *ctx, const char *source_label, const char *target_label,
                             const char *relationship, const char *source_pk_col, const char *target_pk_col,
                             const char *source_fk_col, const char *target_fk_col, const char *feature_name);
static void cleanup_gnn_context(gnn_context *ctx);

static AggregateContext *init_aggregate_context(void);
static void reset_aggregate_context(AggregateContext *agg_context);

static List *find_neighbors(gnn_context *ctx, int64 source_id);
static void perform_gnn(gnn_context *ctx);

static double mean(double arg1, double arg2)
{
    return (arg1 + arg2) / 2.0;
}

static void init_gnn_context(gnn_context *ctx, const char *source_label, const char *target_label,
                             const char *relationship, const char *source_pk_col, const char *target_pk_col,
                             const char *source_fk_col, const char *target_fk_col, const char *feature_name)
{
    Oid source_relid = get_relname_relid(source_label, get_namespace_oid("public", false));
    Oid target_relid = get_relname_relid(target_label, get_namespace_oid("public", false));
    Oid rs_relid = get_relname_relid(relationship, get_namespace_oid("public", false));

    if (!OidIsValid(source_relid) || !OidIsValid(target_relid) || !OidIsValid(rs_relid))
    {
        elog(ERROR, "Required tables do not exist");
    }

    ctx->source_rel = table_open(source_relid, RowExclusiveLock); // Because we will be updating
    ctx->target_rel = table_open(target_relid, AccessShareLock);
    ctx->rs_rel = table_open(rs_relid, AccessShareLock);
    if (!RelationIsValid(ctx->source_rel) || !RelationIsValid(ctx->target_rel) || !RelationIsValid(ctx->rs_rel))
    {
        elog(ERROR, "Failed to open required relations");
    }

    ctx->target_idx = index_open(ctx->target_rel->rd_pkindex, AccessShareLock);
    if (!RelationIsValid(ctx->target_idx))
    {
        elog(ERROR, "Failed to open index on target relation, no primary key found");
    }

    // TODO: open the index for the relationship relation
    ctx->rs_idx = NULL;

    // Since we will be writing feature aggregate to source relation
    ctx->indstate = CatalogOpenIndexes(ctx->source_rel);

    ctx->source_label = pstrdup(source_label);
    ctx->target_label = pstrdup(target_label);
    ctx->relationship = pstrdup(relationship);
    ctx->source_pk_col = pstrdup(source_pk_col);
    ctx->target_pk_col = pstrdup(target_pk_col);
    ctx->source_fk_col = pstrdup(source_fk_col);
    ctx->target_fk_col = pstrdup(target_fk_col);
    ctx->feature_name = pstrdup(feature_name);
}

static void cleanup_gnn_context(gnn_context *ctx)
{
    if (RelationIsValid(ctx->source_rel))
        table_close(ctx->source_rel, RowExclusiveLock);
    if (RelationIsValid(ctx->target_rel))
        table_close(ctx->target_rel, AccessShareLock);
    if (RelationIsValid(ctx->rs_rel))
        table_close(ctx->rs_rel, AccessShareLock);
    if (RelationIsValid(ctx->target_idx))
        index_close(ctx->target_idx, AccessShareLock);
    if (RelationIsValid(ctx->rs_idx))
        index_close(ctx->rs_idx, AccessShareLock);
    
    /* Close the index state */
    if (ctx->indstate != NULL)
        CatalogCloseIndexes(ctx->indstate);

    /* Free the context */
    pfree(ctx);
}

static AggregateContext *init_aggregate_context(void)
{
    AggregateContext *agg_context;
    agg_context = (AggregateContext *) palloc0(sizeof(AggregateContext));
    
    /* Initialize the aggregate context */
    agg_context->feature_sum = 0;
    agg_context->neighbor_count = 0;

    return agg_context;
}

static void reset_aggregate_context(AggregateContext *agg_context)
{
    if (agg_context)
    {
        agg_context->feature_sum = 0;
        agg_context->neighbor_count = 0;
    }
}

static void perform_gnn(gnn_context *ctx)
{
    TableScanDesc scandesc;
    TupleDesc tupdesc;
    HeapTuple tuple;
    AggregateContext *agg_context;
    AttrNumber id_attnum;
    AttrNumber feature_attnum;
    AttrNumber agg_attnum;
    int num_atts = ctx->source_rel->rd_att->natts;
    Datum *values;
    bool *nulls;
    bool *replaces;
    
    scandesc = table_beginscan(ctx->source_rel, GetLatestSnapshot(), 0, NULL);
    tupdesc = RelationGetDescr(ctx->source_rel);

    id_attnum = get_attnum(ctx->source_rel->rd_id, ctx->source_pk_col);
    feature_attnum = get_attnum(ctx->source_rel->rd_id, ctx->feature_name);
    agg_attnum = get_attnum(ctx->source_rel->rd_id, "feature_agg"); //Hardcoded for now, can be parameterized later

    values = (Datum *) palloc0(num_atts * sizeof(Datum));
    nulls = (bool *) palloc0(num_atts * sizeof(bool));
    replaces = (bool *) palloc0(num_atts * sizeof(bool));

    agg_context = init_aggregate_context();

    while ((tuple = heap_getnext(scandesc, ForwardScanDirection)) != NULL)
    {
        List *neighbor_ids;
        int64 id;
        Datum agg_col;

        HeapTuple new_tuple;
        bool isnull = false;
        ListCell *lc;

        double feature_aggregate;

        /* Get the id */
        id = DatumGetInt64(heap_getattr(tuple, id_attnum, tupdesc, &isnull));
        if (isnull)
        {
            if (debug)
                elog(ERROR, "Skipped tuple with NULL ID");
            continue;
        }
        
        /* Get the existing aggregate value */
        agg_col = heap_getattr(tuple, agg_attnum, tupdesc, &isnull);
        agg_context->feature_sum = DatumGetFloat8(agg_col);
        if (agg_context->feature_sum == 0)
        {
            /* Initialize it with the value in feature column */
            agg_context->feature_sum = DatumGetInt64(heap_getattr(tuple, feature_attnum, tupdesc, &isnull));
        }

        /* Find neighbors */
        neighbor_ids = find_neighbors(ctx, id);

        /* Aggregate feature values from neighbors */
        foreach(lc, neighbor_ids)
        {
            int64 neighbor_id = lfirst_int(lc);
            TupleTableSlot *slot;
            double neighbor_fagg_val;
            ScanKeyData skey[1];
            IndexScanDesc idx_scandesc;
        
            /* Fetch neighbor tuple using index */
            ScanKeyInit(&skey[0], 1, BTEqualStrategyNumber, F_INT8EQ, Int64GetDatum(neighbor_id));
            idx_scandesc = index_beginscan(ctx->target_rel, ctx->target_idx, GetLatestSnapshot(), 1, 0);
            index_rescan(idx_scandesc, skey, 1, NULL, 0);

            slot = table_slot_create(ctx->target_rel, NULL);

            if (index_getnext_slot(idx_scandesc, ForwardScanDirection, slot))
            {
                neighbor_fagg_val = DatumGetFloat8(slot_getattr(slot, agg_attnum, &isnull));

                if (neighbor_fagg_val == 0)
                    neighbor_fagg_val = DatumGetInt64(slot_getattr(slot, feature_attnum, &isnull));

                /* Adjust the context */
                agg_context->feature_sum += neighbor_fagg_val;
                agg_context->neighbor_count++;
            }

            ExecDropSingleTupleTableSlot(slot);
            index_endscan(idx_scandesc);
        }

        /* Apply the mean and store it */
        if (agg_context->neighbor_count > 0)
            feature_aggregate = agg_context->feature_sum / (agg_context->neighbor_count + 1); // +1 for self
        else
            feature_aggregate = agg_context->feature_sum; // No neighbors, use own value

        values[agg_attnum - 1] = Float8GetDatum(feature_aggregate);
        nulls[agg_attnum - 1] = false;
        replaces[agg_attnum - 1] = true;

        new_tuple = heap_modify_tuple(tuple, tupdesc, values, nulls, replaces);
        CatalogTupleUpdateWithInfo(ctx->source_rel, &new_tuple->t_self, new_tuple, ctx->indstate);

        if (debug)
            elog(INFO, "Source ID: %ld, Feature Aggregate: %f, Neighbor Count: %ld",
                        id, feature_aggregate, agg_context->neighbor_count);

        /* Reset aggregate context for next person */
        reset_aggregate_context(agg_context);
    }

    table_endscan(scandesc);

    /* Free the aggregate context */
    pfree(agg_context);
    pfree(values);
    pfree(nulls);
    pfree(replaces);

    CommandCounterIncrement();
}

/* returns person ids of neighbors */
static List *find_neighbors(gnn_context *ctx, int64 source_id)
{
    ScanKeyData skey[1];
    TableScanDesc scandesc;
    HeapTuple tuple;
    List *neighbor_ids;
    AttrNumber source_id_attnum = get_attnum(ctx->rs_rel->rd_id, ctx->source_fk_col);
    AttrNumber target_id_attnum = get_attnum(ctx->rs_rel->rd_id, ctx->target_fk_col);

    ScanKeyInit(&skey[0],
                source_id_attnum,
                BTEqualStrategyNumber,
                F_INT8EQ,
                Int64GetDatum(source_id));
    
    // TODO: if there is rs_index, use it
    scandesc = table_beginscan(ctx->rs_rel, GetLatestSnapshot(), 1, skey);
    
    neighbor_ids = NIL;
    while ((tuple = heap_getnext(scandesc, ForwardScanDirection)) != NULL)
    {
        bool isnull = false;
        Datum neighbor_id;

        neighbor_id = heap_getattr(tuple, target_id_attnum, RelationGetDescr(ctx->rs_rel), &isnull);
        if (isnull)
        {
            if (debug)
                elog(ERROR, "Found NULL neighbor ID for source ID %ld", source_id);
            continue;
        }

        neighbor_ids = lappend_int(neighbor_ids, DatumGetInt64(neighbor_id));
    }

    table_endscan(scandesc);
    return neighbor_ids;
}

PG_FUNCTION_INFO_V1(gnn_prototype);
Datum gnn_prototype(PG_FUNCTION_ARGS)
{
    char *source_label = text_to_cstring(PG_GETARG_TEXT_PP(0));
    char *target_label = text_to_cstring(PG_GETARG_TEXT_PP(1));
    char *rs_name = text_to_cstring(PG_GETARG_TEXT_PP(2));

    char *source_pk_col = text_to_cstring(PG_GETARG_TEXT_PP(3));
    char *target_pk_col = text_to_cstring(PG_GETARG_TEXT_PP(4));

    char *source_fk_col = text_to_cstring(PG_GETARG_TEXT_PP(5));
    char *target_fk_col = text_to_cstring(PG_GETARG_TEXT_PP(6));

    char *feature_name = text_to_cstring(PG_GETARG_TEXT_PP(7));
    int iterations = PG_GETARG_INT32(8);
    int i;

    gnn_context *ctx;

    if (iterations < 1)
    {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("Iterations must be at least 1")));
    }

    elog(INFO, "Starting GNN prototype for (%s)-[%s]->(%s) with feature %s and %d iterations\n",
         source_label, rs_name, target_label, feature_name, iterations);

    ctx = (gnn_context *) palloc(sizeof(gnn_context));
    init_gnn_context(ctx, source_label, target_label, rs_name,
                     source_pk_col, target_pk_col,
                     source_fk_col, target_fk_col,
                     feature_name);
                     
    for (i = 0; i < iterations; i++)
    {
        if (debug)
            elog(INFO, "Iteration %d: ", i + 1);

        perform_gnn(ctx);
    }

    cleanup_gnn_context(ctx);

    PG_RETURN_VOID();
}