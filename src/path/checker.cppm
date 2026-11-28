export module formlight.checker;

import std;
import formlight.geometry;

export namespace formlight
{
struct CheckerTexture
{
    double cell_size{1};
    Vec3 even{1, 1, 1}, odd{};

    void fields(this auto &self, auto &&field)
    {
        field("cell_size", self.cell_size);
        field("even", self.even);
        field("odd", self.odd);
    }

    void validate() const
    {
        if (!(cell_size > 0 && std::isfinite(cell_size)))
        {
            throw std::invalid_argument("Checker cell_size must be finite and positive");
        }
        for (const double channel : {even.x, even.y, even.z, odd.x, odd.y, odd.z})
        {
            if (!(channel >= 0 && channel <= 1))
            {
                throw std::invalid_argument("Checker colors must be in [0, 1]");
            }
        }
    }

    [[nodiscard]] Vec3 sample(Vec3 point) const noexcept
    {
        const auto odd_axis = [&](double coordinate)
        {
            // remquo gives the low bits of the nearest quotient even when the full index exceeds int64.
            // A negative remainder puts floor(coordinate / cell_size) one cell below that quotient.
            int quotient{};
            const double remainder = std::remquo(coordinate, cell_size, &quotient);
            return (quotient % 2 != 0) != (remainder < 0);
        };
        return (odd_axis(point.x) ^ odd_axis(point.y) ^ odd_axis(point.z)) ? odd : even;
    }
};
} // namespace formlight
