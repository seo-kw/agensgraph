/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#ifndef AG_CYPHER_LABEL_EXPR_H
#define AG_CYPHER_LABEL_EXPR_H

#include "nodes/nodes.h"
#include "utils/syscache.h"
#include "catalog/ag_label.h"
#include "commands/graphcmds.h"

#define FIRST_LABEL_NAME(label_expr) \
    (list_length((label_expr)->label_names) == 0) \
    ? NULL : linitial((label_expr)->label_names)

// TODO: more readable name LABEL_EXPR_HAS_LABEL returning the oppposite?
#define LABEL_EXPR_LENGTH(label_expr) (list_length((label_expr)->label_names))
#define LABEL_EXPR_IS_EMPTY(label_expr) (LABEL_EXPR_LENGTH((label_expr)) == 0)
#define LABEL_EXPR_LABEL_NAMES(label_expr) (list_copy_deep((label_expr)->label_names))
//((label_expr)->label_names)


#endif
