-- This data is intended for performance testing and benchmarking.
CREATE TABLE Person (
    id SERIAL PRIMARY KEY,
    name TEXT NOT NULL,
    age INT NOT NULL,
    city TEXT,
    feature_mean FLOAT8 NOT NULL DEFAULT 0
);

CREATE TABLE Friends (
    person_id INT NOT NULL REFERENCES Person(id) ON DELETE CASCADE,
    friend_id INT NOT NULL REFERENCES Person(id) ON DELETE CASCADE,
    PRIMARY KEY (person_id, friend_id),
    CHECK (person_id <> friend_id)
);

\gset num_nodes 1000000
\gset num_edges 5000000
INSERT INTO Person (name, age, city)
SELECT
    'Person_' || gs AS name,
    (floor(random()*50)+18)::int AS age,  -- ages between 18 and 67
    cities[(floor(random()*array_length(cities,1))+1)::int] AS city
FROM generate_series(1, num_nodes) gs,
     (SELECT ARRAY[
        'New York','Los Angeles','Chicago','Boston','San Francisco',
        'Houston','Phoenix','Philadelphia','San Diego','Dallas',
        'Seattle','Austin','Denver','Miami','Atlanta'
     ] AS cities) t;


INSERT INTO Friends (person_id, friend_id)
SELECT *
FROM (
    SELECT
        (floor(random()*num_nodes)+1)::int AS person_id,
        (floor(random()*num_nodes)+1)::int AS friend_id
    FROM generate_series(1, num_edges)
) t
WHERE t.person_id <> t.friend_id
ON CONFLICT DO NOTHING;
