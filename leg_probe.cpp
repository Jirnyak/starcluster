// (§48, ВРЕМЕННЫЙ ЗАМЕР) Цена плеча против ЗАПОЛНЕНИЯ БУНКЕРА.
//
// После правки оценки (§48) мёртвой массой манёвра честно считается и топливо,
// уже залитое в бункер. У стартового корпуса бункер тяжелее самого корпуса,
// поэтому вопрос простой: сколько стоит плечо, если лететь с полным бункером,
// и сколько — если везти только нужное.

#include "game.h"
#include "ship.h"

#include <cstdio>
#include <cstdlib>
#include <cmath>

int main(int argc, char** argv) {
    const double distance = argc > 1 ? std::atof(argv[1]) : 7.0;

    Game g;
    g.seed = 20260813u;
    g.init(200);
    Ship ship = g.agents[size_t(g.playerAgent)].ship;

    const double fullFuel = shipFuelMix(ship).mass;
    const double fullProp = shipPropellantMix(ship).mass;
    std::printf("корпус: сухая масса %.1f, трюм %.1f, бункер %.1f массы, бак %.1f массы\n",
                ship.dryMass, ship.cargoCapacity, fullFuel, fullProp);
    std::printf("плечо %.1f ly\n\n", distance);
    std::printf("  %-22s %10s %10s %10s\n", "топлива в бункере", "нужно РТ", "нужно топл", "масса борта");

    const double shares[] = {1.0, 0.5, 0.25, 0.10};
    for (int i = 0; i < 4; ++i) {
        Ship s = ship;
        // Сливаем бункер до доли: тот же состав, меньше массы.
        for (size_t f = 0; f < s.fuel.size(); ++f) s.fuel[f].amount *= shares[i];
        const RouteCost need = shipEstimateRoute(s, distance, 1.0, 1.0);
        std::printf("  %-22.2f %10.2f %10.2f %10.1f%s\n",
                    shipFuelMix(s).mass, need.propellantMass, need.fuelMass,
                    shipTotalMass(s), need.feasible ? "" : "  НЕДОСТИЖИМО");
    }

    // Крейсерская доля: лететь медленнее объективно дешевле (§12), и ручка уже
    // есть — по умолчанию она стоит на самом дорогом делении.
    std::printf("\n  %-22s %10s %10s %10s\n", "крейсер, доля потолка", "нужно РТ", "скорость", "годы в пути");
    const double cruises[] = {1.0, 0.7, 0.5, 0.3, 0.2};
    for (int i = 0; i < 5; ++i) {
        Ship s = ship;
        s.cruiseFraction = cruises[i];
        shipTuneDrive(s, 1.0, 1.0);
        const RouteCost need = shipEstimateRoute(s, distance, 1.0, 1.0);
        const double v = shipCruiseSpeed(s);
        std::printf("  %-22.2f %10.2f %10.3f %10.1f%s\n",
                    cruises[i], need.propellantMass, v, distance / std::max(1e-9, v),
                    need.feasible ? "" : "  НЕДОСТИЖИМО");
    }
    return 0;
}
