/* contrib/gnn/gnn--0.1.sql */


-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "CREATE EXTENSION gnn" to load this file. \quit

-- We need system-wide python module installation to use external modules
-- pip install torch --break-system-packages [your_libraries]
CREATE EXTENSION IF NOT EXISTS plpython3u;

CREATE FUNCTION get_aggregate_result(input float8[][], fname TEXT) 
    RETURNS float8[]
LANGUAGE C STRICT
AS 'MODULE_PATHNAME';

CREATE OR REPLACE FUNCTION get_mean (input float8[][])
    RETURNS float8[]
AS $$
import torch

x = torch.tensor(
    input, dtype=torch.float
)

m = x.mean(dim=1)

l = m.tolist()

return l
$$ LANGUAGE plpython3u;











----------------------
-- playground below --
----------------------


-- test
-- select tensor_output(ARRAY[ARRAY[1.1,1.2,1.3],ARRAY[2.1,2.2,2.3]]);
-- select tensor_output2(ARRAY[ARRAY[1.1,1.2,1.3],ARRAY[2.1,2.2,2.3]]);
-- select tensor_output3(ARRAY[ARRAY[1.1,1.2,1.3],ARRAY[2.1,2.2,2.3]]);
-- select tensor_output4(ARRAY[ARRAY[1.1,1.2,1.3],ARRAY[2.1,2.2,2.3]]);
-- CREATE TYPE t AS (
--     f   float8[]
-- );


-- return will be a tuple
--  tensor_output3 
-- ----------------
--  ("{1,2}")
-- (1 row)
CREATE FUNCTION tensor_output3 (input float8[][])
    RETURNS t
AS $$
import torch
x = torch.tensor([
    [1.0, 2.0],
    [3.0, 4.0],
    [5.0, 6.0],
    [7.0, 8.0],
    [2.0, 1.0],
    [0.5, 1.5],
])
l = x.tolist()

class t: passz
t.f = l[0]
return t

$$ LANGUAGE plpython3u;



-- return will be a list
--  tensor_output4 
-- ----------------
--  {1,2}
-- (1 row)

CREATE FUNCTION tensor_output4 (input float8[][])
    RETURNS float8[]
AS $$
import torch
x = torch.tensor([
    [1.0, 2.0],
    [3.0, 4.0],
    [5.0, 6.0],
    [7.0, 8.0],
    [2.0, 1.0],
    [0.5, 1.5],
])
l = x.tolist()

return l[0]

$$ LANGUAGE plpython3u;


-- transform parameter to tensor
-- select tensor_output5(ARRAY[ARRAY[1.1,1.2,1.3],ARRAY[2.1,2.2,2.3]]);
CREATE FUNCTION tensor_output5 (input float8[][])
    RETURNS float8[]
AS $$
import torch
x = torch.tensor(
    input, dtype=torch.float
)
l = x.tolist()

return l[0]

$$ LANGUAGE plpython3u;




CREATE FUNCTION torch_mean(anyarray, dimension integer) 
    RETURNS anyarray
LANGUAGE C STRICT
AS 'MODULE_PATHNAME';

CREATE FUNCTION call_torch_1d(oid integer) 
    RETURNS double precision
LANGUAGE C STRICT
AS 'MODULE_PATHNAME';

-- select torch_mean_float8('{{1,2,3},{4,5,6}}'::double precision[][]);
-- select torch_mean_float8('{1,2,3}'::double precision[]);
-- ERROR:  PL/Python functions cannot accept type anyarray

DROP FUNCTION IF EXISTS torch_mean_float8;
CREATE FUNCTION torch_mean_float8(features double precision[], dimension integer)
    RETURNS double precision
AS $$
import torch
neighbor_features = torch.tensor(features, dtype=torch.float32)

return neighbor_features.mean(dim=dimension).tolist()
$$ LANGUAGE plpython3u;


-- Graph Isomorphism Network
-- GINConv layers use small MLPs to update node features after sum aggregation
CREATE FUNCTION sample_gin(features float[], features_count integer)
    RETURNS float[]
AS $$

import torch
import torch.nn.functional as F
from torch_geometric.data import Data
from torch_geometric.nn import GINConv, global_add_pool
from torch_geometric.loader import DataLoader
from torch.nn import Sequential, Linear, ReLU

data_list = []

edge_index1 = torch.tensor([[0, 1, 1, 2], [1, 0, 2, 1]], dtype=torch.long)
x1 = torch.tensor([[1], [2], [3]], dtype=torch.float)
y1 = torch.tensor([0])
data_list.append(Data(x=x1, edge_index=edge_index1, y=y1))

edge_index2 = torch.tensor([[0, 1], [1, 0]], dtype=torch.long)
x2 = torch.tensor([[4], [5]], dtype=torch.float)
y2 = torch.tensor([1])
data_list.append(Data(x=x2, edge_index=edge_index2, y=y2))

loader = DataLoader(data_list, batch_size=2)

class GIN(torch.nn.Module):
    def __init__(self):
        super(GIN, self).__init__()
        nn1 = Sequential(Linear(1, 16), ReLU(), Linear(16, 16))
        self.conv1 = GINConv(nn1)
        nn2 = Sequential(Linear(16, 16), ReLU(), Linear(16, 16))
        self.conv2 = GINConv(nn2)
        self.lin = Linear(16, 2)

    def forward(self, data):
        x, edge_index, batch = data.x, data.edge_index, data.batch
        x = self.conv1(x, edge_index)
        x = F.relu(x)
        x = self.conv2(x, edge_index)
        x = F.relu(x)
        x = global_add_pool(x, batch)
        out = self.lin(x)
        return out

model = GIN()
optimizer = torch.optim.Adam(model.parameters(), lr=0.01)
loss_fn = torch.nn.CrossEntropyLoss()

model.train()
for epoch in range(50):
    for batch in loader:
        optimizer.zero_grad()
        out = model(batch)
        loss = loss_fn(out, batch.y)
        loss.backward()
        optimizer.step()

model.eval()
with torch.no_grad():
    for batch in loader:
        pred = model(batch).argmax(dim=1)


$$ LANGUAGE plpython3u;


-- PNA: Combine multiple aggregators
CREATE FUNCTION sample_pna(features float[], features_count integer)
    RETURNS float[]
AS $$

import torch
import torch.nn as nn
import torch.nn.functional as F
from torch_geometric.data import Data
from torch_geometric.nn import PNAConv
from torch_geometric.utils import degree

edge_index = torch.tensor([
    [0, 1, 2, 3, 0, 2],
    [1, 0, 3, 2, 2, 0]
], dtype=torch.long)

x = torch.tensor([
    [1, 0, 1],
    [0, 1, 1],
    [1, 1, 0],
    [0, 0, 1]
], dtype=torch.float)

y = torch.tensor([0, 1, 0, 1], dtype=torch.long)

data = Data(x=x, edge_index=edge_index, y=y)

deg = degree(data.edge_index[0], num_nodes=data.num_nodes)

class SimplePNA(nn.Module):
    def __init__(self):
        super(SimplePNA, self).__init__()
        aggregators = ['mean', 'max', 'min', 'std']      -- aggregation functions
        scalers = ['identity', 'amplification', 'attenuation']  -- degree scalers

        self.conv1 = PNAConv(in_channels=3, out_channels=8,
                             aggregators=aggregators,
                             scalers=scalers,
                             deg=deg,
                             towers=1,
                             pre_layers=1,
                             post_layers=1,
                             divide_input=False)

        self.conv2 = PNAConv(in_channels=8, out_channels=2,
                             aggregators=aggregators,
                             scalers=scalers,
                             deg=deg,
                             towers=1,
                             pre_layers=1,
                             post_layers=1,
                             divide_input=False)

    def forward(self, data):
        x, edge_index = data.x, data.edge_index
        x = self.conv1(x, edge_index)
        x = F.relu(x)
        x = self.conv2(x, edge_index)
        return x

model = SimplePNA()
optimizer = torch.optim.Adam(model.parameters(), lr=0.01)
loss_fn = nn.CrossEntropyLoss()

for epoch in range(20):
    optimizer.zero_grad()
    out = model(data)
    loss = loss_fn(out, data.y)
    loss.backward()
    optimizer.step()

pred = model(data).argmax(dim=1)


$$ LANGUAGE plpython3u;

-- GAT
-- GAT uses attention mechanisms to assign different weights to neighbors during aggregation.
-- Instead of treating all neighbors equally (like in GCN or GraphSAGE mean), 
-- it learns importance scores dynamically.
CREATE FUNCTION sample_gat(features float[], features_count integer)
    RETURNS float[]
AS $$
import torch
import torch.nn as nn
import torch.nn.functional as F
from torch_geometric.data import Data
from torch_geometric.nn import GATConv

edge_index = torch.tensor([
    [0, 1, 2, 3, 0, 2],
    [1, 0, 3, 2, 2, 0]
], dtype=torch.long)

x = torch.tensor([
    [1, 0, 1],
    [0, 1, 1],
    [1, 1, 0],
    [0, 0, 1]
], dtype=torch.float)

y = torch.tensor([0, 1, 0, 1], dtype=torch.long)

data = Data(x=x, edge_index=edge_index, y=y)

class SimpleGAT(nn.Module):
    def __init__(self):
        super(SimpleGAT, self).__init__()
        self.conv1 = GATConv(in_channels=3, out_channels=4, heads=2, concat=True) -- output will be 4*2=8 features
        self.conv2 = GATConv(in_channels=8, out_channels=2, heads=1, concat=False) -- final output 2 classes

    def forward(self, data):
        x, edge_index = data.x, data.edge_index
        x = self.conv1(x, edge_index)
        x = F.elu(x)
        x = self.conv2(x, edge_index)
        return x

model = SimpleGAT()
optimizer = torch.optim.Adam(model.parameters(), lr=0.01)
loss_fn = nn.CrossEntropyLoss()

for epoch in range(20):
    optimizer.zero_grad()
    out = model(data)
    loss = loss_fn(out, data.y)
    loss.backward()
    optimizer.step()

pred = model(data).argmax(dim=1)
$$ LANGUAGE plpython3u;


-- SAGEConv
-- The aggregation scheme to use. 
-- Any aggregation of torch_geometric.nn.aggr can be used, 
-- e.g., "mean", "max", or "lstm". (default: "mean")
CREATE FUNCTION sample_sage(features float[], features_count integer)
    RETURNS float[]
AS $$
import torch
import torch.nn as nn
import torch.nn.functional as F
from torch_geometric.data import Data
from torch_geometric.nn import SAGEConv

edge_index = torch.tensor([
    [0, 1, 2, 3, 0, 2],
    [1, 0, 3, 2, 2, 0]
], dtype=torch.long)

x = torch.tensor([
    [1, 0, 1],
    [0, 1, 1],
    [1, 1, 0],
    [0, 0, 1]
], dtype=torch.float)

y = torch.tensor([0, 1, 0, 1], dtype=torch.long)

data = Data(x=x, edge_index=edge_index, y=y)

class SimpleGraphSAGE(nn.Module):
    def __init__(self):
        super(SimpleGraphSAGE, self).__init__()
        self.conv1 = SAGEConv(in_channels=3, out_channels=4, aggr='mean')
        self.conv2 = SAGEConv(in_channels=4, out_channels=2, aggr='mean')

    def forward(self, data):
        x, edge_index = data.x, data.edge_index
        x = self.conv1(x, edge_index)
        x = F.relu(x)
        x = self.conv2(x, edge_index)
        return x

model = SimpleGraphSAGE()
optimizer = torch.optim.Adam(model.parameters(), lr=0.01)
loss_fn = nn.CrossEntropyLoss()

for epoch in range(20):
    optimizer.zero_grad()
    out = model(data)
    loss = loss_fn(out, data.y)
    loss.backward()
    optimizer.step()

pred = model(data).argmax(dim=1)

$$ LANGUAGE plpython3u;



-- gcn 
-- mean aggregation only
CREATE FUNCTION sample_gcn(features float[], features_count integer)
    RETURNS float[]
AS $$

import torch
import torch.nn.functional as F
from torch_geometric.data import Data
from torch_geometric.nn import GCNConv

x = torch.tensor([[1, 2, 3],
                  [4, 5, 6],
                  [7, 8, 9],
                  [10, 11, 12]], dtype=torch.float)

edge_index = torch.tensor([[0, 1, 2, 3, 0, 1],
                           [1, 0, 3, 2, 2, 3]], dtype=torch.long)

y = torch.tensor([0, 1, 0, 1])

data = Data(x=x, edge_index=edge_index, y=y)

class SimpleGCN(torch.nn.Module):
    def __init__(self):
        super(SimpleGCN, self).__init__()
        self.conv1 = GCNConv(3, 4)
        self.conv2 = GCNConv(4, 2)

    def forward(self, data):
        x, edge_index = data.x, data.edge_index
        x = self.conv1(x, edge_index)
        x = F.relu(x)
        x = self.conv2(x, edge_index)
        return F.log_softmax(x, dim=1)

model = SimpleGCN()
optimizer = torch.optim.Adam(model.parameters(), lr=0.01)

model.train()
for epoch in range(100):
    optimizer.zero_grad()
    out = model(data)
    loss = F.nll_loss(out, data.y)
    loss.backward()
    optimizer.step()

model.eval()
pred = model(data).max(1)[1]

return pred.tolist()
$$ LANGUAGE plpython3u;
