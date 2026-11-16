# Frontier Classic doorgeefserver (voor je NAS)

Met deze server kunnen spelers samen spelen **zonder IP-adressen** en **zonder port forwarding bij de spelers thuis**.
De host opent een spel onder zijn naam, de anderen vullen die naam in, en de server geeft het spelverkeer door.

De server kijkt niet in de speldata, slaat niets op schijf op en heeft geen accounts.

## Wat je nodig hebt

- Een NAS (of andere computer) met Docker die aan blijft staan. Werkt op Intel/AMD en ARM.
- **Een keer** port forwarding in je eigen router: TCP-poort **47800** naar het lokale adres van de NAS.
- Een adres waarop de NAS van buiten bereikbaar is: je publieke IP-adres, of beter een DDNS-naam
  (bijv. Synology: Configuratiescherm > Externe toegang > DDNS, je krijgt dan iets als `mijnnas.synology.me`).

Gebruik: ongeveer **1-3 MB RAM** (de limiet in `docker-compose.yml` staat ruim op 64 MB), bijna geen CPU,
image van ongeveer 1 MB. Al het spelverkeer loopt via je NAS: reken op enkele tot tientallen kB/s per speler.

## Starten

Zet deze map (met `relay.cpp`, `Dockerfile` en `docker-compose.yml`) op de NAS.

**Via SSH / terminal** (in deze map):

```
docker compose up -d --build
```

**Synology Container Manager:** Project > Maken > kies deze map > de `docker-compose.yml` wordt herkend > Volgende > Gereed.

**Andere NAS (QNAP, Unraid, TrueNAS, Portainer):** maak een "stack" of "compose project" van deze map.
De NAS heeft bij de eerste keer bouwen internet nodig (om de kleine Alpine-bouwomgeving te downloaden).

Controleren of hij draait:

```
docker compose logs
```

Je ziet dan `Frontier Classic relay listening on TCP port 47800`, en later regels als `room opened: yfer`.

## In het spel

Iedere speler doet dit een keer: **Multiplayer > Via server > Server** en vult het adres van de NAS in
(bijv. `mijnnas.synology.me`; met `:poort` erachter als je een andere poort dan 47800 gebruikt).

- **Hosten:** Multiplayer > Spel hosten > instellingen > Open lobby. De lobby toont je naam.
- **Meedoen:** Multiplayer > Meedoen > naam van de host invullen > Verbinden.

Hoofdletters maken niet uit. Twee hosts kunnen niet tegelijk dezelfde naam gebruiken.

## Andere poort

Verander in `docker-compose.yml` alleen het **linker** getal, bijv. `"50000:47800/tcp"`, stuur die poort door in je router
en laat spelers `adres:50000` invullen.

## Stoppen, bijwerken, verwijderen

```
docker compose down                 # stoppen
docker compose up -d --build        # bijwerken na een nieuwe relay.cpp
docker image rm frontier-relay      # image verwijderen
```

## Veiligheid

- De container draait zonder rechten (geen root, alleen-lezen bestandssysteem, geen extra capabilities) en met een geheugenlimiet.
- Er staat alleen deze ene poort open. De server accepteert maximaal 400 verbindingen en 100 spellen tegelijk
  en verbreekt verbindingen die zich niet binnen 15 seconden melden.
- Iedereen die het adres kent en een hostnaam raadt, kan proberen mee te doen in een open lobby.
  De host ziet in de lobby wie er binnen is en start pas als het klopt.
