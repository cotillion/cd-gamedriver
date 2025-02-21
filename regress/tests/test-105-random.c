#pragma strict_types

void
create()
{
    mapping res = ([]);
    for (int i = 0; i < 100000; i++)
    {
        res[random(100000)]++;
    }

    if (m_sizeof(res) < 60000 || m_sizeof(res) > 70000)
        throw("random() is not random enough " + m_sizeof(res) + "\n");
    int tripped = 0;
    foreach (int i, int j : res)
    {
        if (j > 7) {
            tripped++;
        }
    }
    if (tripped > 5)
        throw("random() is not random enough tripped = " + tripped + "\n");

    for (int i = 0; i < 100; i++)
    {
        int seed = random(100000000000000);
        int first = random(10000000, seed);
        for (int j = 0; j < 100; j++)
            if (random(10000000, seed) != first)
                write("random() not keep seeds\n");
    }

    for (int i = 0; i < 10000; i++)
    {
        float rand = rnd();
        if (rand < 0.0 || rand > 1.0)
        {
            throw("rnd() not in range 0.0-1.0\n");
        }
    }
}
