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

**Portainer:** zie het stuk "Via Portainer" hieronder.

**Andere NAS (QNAP, Unraid, TrueNAS):** maak een "stack" of "compose project" van deze map.
De NAS heeft bij de eerste keer bouwen internet nodig (om de kleine Alpine-bouwomgeving te downloaden).

Controleren of hij draait:

```
docker compose logs
```

Je ziet dan `Frontier Classic relay listening on TCP port 47800`, en later regels als `room opened: yfer`.

## Via Portainer

De **Web editor** van Portainer werkt hier niet: daar plak je alleen de compose-tekst, en de server moet
gebouwd worden uit `relay.cpp`. Gebruik daarom **Repository**: Portainer haalt deze map zelf uit GitHub en bouwt hem op de NAS.

**1. Leessleutel maken op GitHub** (de repo is privé):
GitHub > Settings > Developer settings > Personal access tokens > **Fine-grained tokens** > Generate new token.
- Repository access: **Only select repositories** > `FrontierClassic`
- Permissions > Repository permissions > **Contents: Read-only** (verder niets)
- Kopieer de sleutel (begint met `github_pat_`). Deel hem met niemand.

**2. Stack maken in Portainer:** Stacks > **Add stack**
- Name: `frontier-relay`
- Build method: **Repository**
- Repository URL: `https://github.com/ikbenyfer/FrontierClassic`
- Repository reference: `refs/heads/main`
- Compose path: `server/docker-compose.yml`
- **Authentication** aan: username `ikbenyfer`, Personal Access Token: de sleutel van stap 1
- Optioneel: **GitOps updates** aan (polling, bijv. elke 24h): dan bouwt Portainer de server opnieuw als hij op GitHub verandert
- Klik **Deploy the stack** (de eerste keer bouwen duurt 1 tot 3 minuten)

**3. Controleren:** Containers > `frontier-relay` > **Logs**. Je ziet `Frontier Classic relay listening on TCP port 47800`.

**Lukt het bouwen niet** (bijv. "build is not supported" op een oude Portainer of in een Swarm-omgeving)?
Bouw dan een keer via SSH op de NAS in deze map: `docker compose build`. Daarna kun je in Portainer bij
Web editor een stack maken met dezelfde compose-tekst, maar zonder de regel `build: .`.

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
