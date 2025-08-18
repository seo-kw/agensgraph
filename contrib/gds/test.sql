CREATE TABLE Person (
    id SERIAL PRIMARY KEY,
    name TEXT NOT NULL,
    age INT NOT NULL,
    city TEXT,
    feature_agg FLOAT8 NOT NULL DEFAULT 0
);

CREATE TABLE Friends (
    person_id INT NOT NULL REFERENCES Person(id) ON DELETE CASCADE,
    friend_id INT NOT NULL REFERENCES Person(id) ON DELETE CASCADE,
    PRIMARY KEY (person_id, friend_id),
    CHECK (person_id <> friend_id)
);

INSERT INTO Person (name, age, city) VALUES
('Alice', 25, 'New York'),
('Bob', 30, 'Los Angeles'),
('Charlie', 28, 'Chicago'),
('Diana', 35, 'Boston'),
('Eve', 22, 'San Francisco');


INSERT INTO Friends (person_id, friend_id) VALUES
(1, 2),  -- Alice -> Bob
(2, 3),  -- Bob -> Charlie
(3, 4),  -- Charlie -> Diana
(1, 5),  -- Alice -> Eve
(5, 4);  -- Eve -> Diana
