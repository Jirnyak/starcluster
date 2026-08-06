# Owning Systems

A colony is not a separate subsystem. It is **ownership of a system**, and the
system keeps living exactly as it lived before the deal.

## The whole mechanic

One button, `COLONY` (key `C`), opens the ownership window for the system the
player is looking at:

- the system is not yours: the window is a price tag with its full breakdown
  and a `BUY SYSTEM` button;
- the system is yours: the window is the colony vault, with `DEPOSIT`,
  `WITHDRAW` and `TAKE ALL`.

Nothing else. Founding, settler cargo, material requirements and colony ship
hiring were removed: they were a second, parallel way of doing what buying a
system already does.

## Price

Order of magnitude: **a billion credits** — the tier of capital hulls
(Battlecruiser 5e9). Owning a system is a whole-campaign goal, not a purchase
between runs. Measured over generated clusters the median lands at ~1e9, cheap
frontier rocks at ~2e8, the richest systems at ~1.2e10.

The price is a product of independent factors, each answering "how many times
more valuable is this system than an empty rock":

```text
price = SYSTEM_PRICE_BASE
      * (1 + population   / SYSTEM_PRICE_POP_REF)
      * (1 + industry     / SYSTEM_PRICE_IND_REF)
      * (1 + habitability * SYSTEM_PRICE_HAB_W)
      * (1 + turnover     / SYSTEM_PRICE_TURNOVER_REF)   // elements in the system
      * (1 + infrastructure * W + shipyardLevel * W)     // what is already built
      * (1 + ownerStrength * SYSTEM_PRICE_FOREIGN_W)     // sovereignty premium
```

`turnover` is the system's annual raw output priced by its own market
(`Σ productionRate[i] * prices[i]`). This is why "what elements are in the
system" is contextual and needs no table: a system rich in iridium is worth
more than one rich in sand by exactly the ratio the market already knows.

The window shows every factor, so the player reads *why* the number is what it
is. Constants live in `game.h`; `Game::systemPrice` returns the breakdown.

## Money flows

- the price is paid **in full to the previous owner faction**, which spends it
  on its own expansion and fleet — you are financing the neighbour you bought
  from. Unclaimed systems have no seller and no sovereignty premium;
- the colony accumulates its own profit in `Colony::localLedger` and spends it
  on its own construction queue without the player;
- the player deposits into and withdraws from that vault, but **only while
  docked in that system**. The empire is a route, not a menu line.

## Free market

In a system the player owns, trade with the local market costs nothing: goods,
fuel and propellant are taken and given at price 0, with no tariff and no
licence fee. The trade window says `YOUR COLONY, ALL PRICES 0` and its buttons
read `TAKE` / `GIVE`.

**Supply and price still move.** This is not a balance concession, it is the
same physics: the warehouse is finite, so an owner who strips it raises the
system's `strain`, chokes population growth and cuts their own income. The
limit is built into the model, not bolted on as a cap.

Free trade applies only to the ship the player is flying. Hired ships of the
same faction trade under normal rules — otherwise they would haul free goods
to foreign markets.

Two consequences worth knowing:

- a free purchase is bounded by the **hold**, not by money — otherwise asking
  for "max" would drain the system into a ship that cannot carry it;
- a sale at price 0 pays no licence tariff, so **the turnover quota cannot be
  closed through your own colony**. Trading outside stays mandatory. That is why
  the first-period quota was raised to 10 000 Cr: it has to be a real obligation
  rather than something a settled player never notices (see README, "Trading
  Licence And Quota").

## What is still simulated (unchanged)

`Game::updateColonies` runs for every colony, player-owned or not: population
growth against market strain, industry, infrastructure, defense, income into
the local ledger, and a self-managed construction queue that starts a build
whenever the vault is funded. Colony stockpiles and `ColonySupply` contracts
are untouched.

## Regression cover

`make balance` — `SYSTEM PRICE` (median stays a billion, every factor >= 1),
purchase moves owner and conserves credits, owned market is free but finite,
vault conserves money and is refused from afar.
`make uiclick` — the colony window's buttons actually hit.
