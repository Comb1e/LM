def samples(seed, count, bound):
    mask = (1 << 64) - 1
    result = []
    while len(result) < count:
        seed ^= seed >> 12
        seed ^= (seed << 25) & mask
        seed ^= seed >> 27
        value = seed * 2685821657736338717 & mask
        if value > mask % bound:
            result.append(value % bound)
    return result


def main():
    data = samples(1, 100, 1000)
    print(f"sum={sum(data)}")
    print(f"upper_median={sorted(data)[len(data) // 2]}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
