-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "CREATE EXTENSION gsheets" to load this file. \quit

CREATE FUNCTION gnn_prototype(source_label TEXT, target_label TEXT, relationship TEXT,
                              -- these four params are temp, and may not be required in actual setting
                              source_pk_col TEXT, target_pk_col TEXT,
                              source_fk_col TEXT, target_fk_col TEXT,
                              feature_name TEXT, iterations INT DEFAULT 1)
RETURNS VOID
STRICT
LANGUAGE C
AS 'MODULE_PATHNAME', 'gnn_prototype';