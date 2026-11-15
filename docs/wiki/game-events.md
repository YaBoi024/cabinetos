# Game events

The console can tell one app in your home what is happening in a game: a game
started, paused or ended, an achievement unlocked. That app decides what
happens next: a post to your Discord, the lights dimming while a game runs, a
ping on your phone. The console only ever sends to that one address, and only
while it is on your own network. It never listens.

## What you need

An app on your home network that takes a webhook: [n8n](https://n8n.io),
Home Assistant, Node-RED, or a small script of your own. The example below is
n8n posting to Discord, the one that was tested. The app can be on your home
network or on your [Tailscale](network.md#tailscale) network.

## Turn it on

Settings, **System**, **Game events** (the PIN, if one is set). The keyboard
opens with `http://` already in it: type the rest of your app's address, for
example `http://n8n.local:5678/webhook/games`, and press Done. The console
sends the app a test message first, which names nobody. If the app answers,
the address is saved and the row shows its host. If not, the field says why
and keeps what you typed:

| The field says | It means |
|---|---|
| Not on your home network | The address is not a home or Tailscale address, so the console will not send to it |
| Can't connect | Nothing answered at that address |
| It answered with an error (404) | Something answered, with that error. For n8n, a 404 usually means the workflow is not published yet |
| Its certificate isn't trusted. Use http:// at home. | An `https://` address whose certificate the console does not trust |

With an address saved, pressing the row again opens **Change address** and
**Stop sending**. Stop sending forgets the address, and nothing more is sent.
Off is the start for every console, and while it is off no part of it runs.

## When it sends, and when it doesn't

- **Home addresses only.** Every time it sends, the console looks the address
  up, and every address the name gives must be a home one (`192.168.x.x`,
  `10.x.x.x`, `172.16.x.x` to `172.31.x.x`) or a Tailscale one (`100.64.x.x`
  to `100.127.x.x`), or it sends nothing.
- **A test message first, on every network.** A home address exists on any
  network, a friend's included. So each time the console joins a network, or
  comes back from Sleep, the first thing it sends is the test message.
  Messages about games go only if your app answers it. If your app is down at
  that moment, nothing is sent until the console changes network again, or
  until you press Done on the address again.
- **No network, nothing sent, and nothing kept for later.** A "game started"
  an hour late would be wrong.
- **It never holds up a game.** A message your app does not take within five
  seconds is dropped.

## Playing over LinkUp

When a friend joins your game over [LinkUp](linkup.md), each console sends
only to its own owner's app, under its own person's name. Your console says
the game is one you are hosting; your friend's says it joined one. Neither
names the other person: who your friend is stays theirs. A console that joined
sends only started and ended, and without the system and the cover, since the
game is on the host's server.

## The messages

Each message is JSON, sent by HTTP POST to your address (`Content-Type:
application/json`), one at a time, in order.

| `event` | Sent when | Adds |
|---|---|---|
| `game_started` | the game is on screen | `cover`, `cover_type` |
| `game_paused` | the pause menu (or the power menu) opens over the game | |
| `game_resumed` | it closes and the game goes on | |
| `game_ended` | the game is left, by Exit to Home, Sleep, Restart or Power off | `minutes_played` |
| `achievement_unlocked` | each achievement | `achievement`, `points` |
| `set_completed` | the last achievement in the game | `cover`, `cover_type` |
| `test` | Done on the keyboard, and first on every network | only `format`, `event`, `time` |

Every message but `test` carries these:

| Field | What it is |
|---|---|
| `format` | `1`. Fields can be added, never renamed or removed; a change that breaks that is `2`. |
| `event` | Which message it is, from the table above |
| `time` | The console's local time, with its offset: `2026-11-15T19:32:00-05:00` |
| `person` | The console account playing |
| `game` | The game's name |
| `system` | The system, as the Library names it |
| `rom_id` | The game's number on your RomM server |
| `linkup` | Your LinkUp status: `online`, `offline` (you appear offline) or `unused` |
| `linkup_game` | `none`, `hosting` or `joined`: whether this is a LinkUp game, and which side this console is on |

And, on some:

| Field | On | What it is |
|---|---|---|
| `cover`, `cover_type` | started, every achievement | The box art the console keeps, base64, in whatever format RomM gave it (`image/png`, `image/jpeg`, `image/webp`); left out when the game has none |
| `minutes_played` | ended | Minutes of play the console counted (paused time does not count) |
| `achievement`, `points` | an achievement | Its name and its points |

A started message, with the cover cut short:

```json
{"format":1,"event":"game_started","time":"2026-11-15T19:32:00-05:00","person":"Sam","game":"Contra III: The Alien Wars","system":"Super Nintendo","rom_id":1234,"linkup":"unused","linkup_game":"none","cover_type":"image/png","cover":"iVBORw0KGgo..."}
```

A cover is often a few hundred kilobytes once in base64, so an app that keeps
every message should drop the cover or keep it small.

## Example: n8n to Discord

This workflow posts each person's games to their own Discord channel: a card
with the cover when a game starts, a gold card when every achievement is
unlocked, and one line for the rest. A LinkUp game's card is purple and says
"over LinkUp". A person not on its list goes nowhere, so an account added to
the console later stays quiet until you choose where its games go.

1. **In Discord,** for each person: make a channel (or pick one), then the
   gear beside its name, **Integrations**, **Webhooks**, **New Webhook**,
   **Copy Webhook URL**.
2. **In n8n,** make a new workflow, click the empty canvas, and paste the
   workflow below (Cmd+V or Ctrl+V). Five boxes appear.
3. Open **Sort by person** and change the two lines at the top of its code:
   each person's name exactly as the console shows it, then their Discord
   webhook. Add a line for each person; delete the lines you do not need.
4. Save, then **Publish**. In n8n 2, an edit only reaches the console after
   you publish again, and a just-published workflow can answer 404 for a few
   seconds.
5. **On the console:** Settings, **System**, **Game events**, and type
   `http://` and your n8n's address with `:5678/webhook/games` after it.

The workflow's webhook path is `games`, short on purpose: a webhook n8n makes
itself gets a long random path, which is hard to type with a controller. Its
**Executions** list shows each message the console sent and Discord's answer.

```json
{
  "name": "CabinetOS Game events",
  "nodes": [
    {
      "parameters": {
        "httpMethod": "POST",
        "path": "games",
        "options": {}
      },
      "id": "f0207f5f-7683-4d1c-8bc8-1ecbf1b42ebe",
      "name": "From the console",
      "type": "n8n-nodes-base.webhook",
      "typeVersion": 2.1,
      "position": [
        0,
        0
      ],
      "webhookId": "9c5bc21b-7e04-4858-b26c-a49ac18ef4e0"
    },
    {
      "parameters": {
        "jsCode": "// CabinetOS Game events: each person's games go to their own Discord channel.\n// One line per person: the name exactly as the console shows it, then that\n// person's Discord webhook (in Discord: Edit Channel, Integrations, Webhooks).\n// Anyone not listed goes nowhere, and so does the console's test message.\nconst PEOPLE = {\n  \"First person\": \"https://discord.com/api/webhooks/PASTE-THEIR-WEBHOOK\",\n  \"Second person\": \"https://discord.com/api/webhooks/PASTE-THEIR-WEBHOOK\",\n};\n\nconst CYAN = 0x58E8F6, PURPLE = 0xA07CF0, GOLD = 0xF2C14E;\nconst COVER = { 'image/png': 'png', 'image/jpeg': 'jpg', 'image/webp': 'webp' };\n\nfunction minutes(n) {\n  if (!n) return 'less than a minute';\n  return n === 1 ? '1 minute' : `${n} minutes`;\n}\n\nconst out = [];\nfor (const item of $input.all()) {\n  const m = item.json.body ?? {};\n  const url = PEOPLE[m.person];\n  if (!url) continue;\n  const linkup = m.linkup_game === 'hosting' || m.linkup_game === 'joined';\n  const over = linkup ? ' over LinkUp' : '';\n  const who = `**${m.person}**`, game = `**${m.game}**`;\n  const points = m.points === 1 ? '1 point' : `${m.points} points`;\n\n  // Started and every achievement: a card, with the cover when it came.\n  let card = null;\n  if (m.event === 'game_started') {\n    card = { author: m.linkup_game === 'joined' ? `🎮 ${m.person} joined a LinkUp game`\n                                                 : `🎮 ${m.person} started playing${over}`,\n             color: linkup ? PURPLE : CYAN };\n  } else if (m.event === 'set_completed') {\n    card = { author: `🌟 ${m.person} unlocked every achievement${over}`, color: GOLD };\n  }\n  // The rest: one line each.\n  const line = {\n    game_paused: `⏸️ ${who} paused ${game}${over}`,\n    game_resumed: `▶️ ${who} is back in ${game}${over}`,\n    game_ended: `🛑 ${who} stopped playing ${game}${over} after ${minutes(m.minutes_played)}`,\n    achievement_unlocked: `🏆 ${who} unlocked **${m.achievement}** (${points}) in ${game}${over}`,\n  }[m.event];\n  if (!card && !line) continue;\n\n  const post = { json: { url, cover: false, payload: { allowed_mentions: { parse: [] } } } };\n  if (line) post.json.payload.content = line;\n  if (card) {\n    const embed = { author: { name: card.author }, title: m.game, color: card.color };\n    if (m.system) embed.description = m.system;\n    const type = COVER[m.cover_type];\n    if (m.cover && type) {\n      const file = `cover.${type}`;\n      embed.thumbnail = { url: `attachment://${file}` };\n      post.json.cover = true;\n      post.binary = { cover: { data: m.cover, mimeType: m.cover_type, fileName: file } };\n    }\n    post.json.payload.embeds = [embed];\n  }\n  out.push(post);\n}\nreturn out;\n"
      },
      "id": "f3154338-5292-4c15-8b90-87e8243a63db",
      "name": "Sort by person",
      "type": "n8n-nodes-base.code",
      "typeVersion": 2,
      "position": [
        240,
        0
      ]
    },
    {
      "parameters": {
        "conditions": {
          "options": {
            "caseSensitive": true,
            "leftValue": "",
            "typeValidation": "strict",
            "version": 2
          },
          "conditions": [
            {
              "id": "7b63e266-d792-469a-90ad-de089fbf94c5",
              "leftValue": "={{ $json.cover }}",
              "rightValue": "",
              "operator": {
                "type": "boolean",
                "operation": "true",
                "singleValue": true
              }
            }
          ],
          "combinator": "and"
        },
        "options": {}
      },
      "id": "f2022917-65d9-4b52-85d2-4db83bf035d4",
      "name": "Has a cover?",
      "type": "n8n-nodes-base.if",
      "typeVersion": 2.2,
      "position": [
        480,
        0
      ]
    },
    {
      "parameters": {
        "method": "POST",
        "url": "={{ $json.url }}",
        "sendQuery": true,
        "queryParameters": {
          "parameters": [
            {
              "name": "wait",
              "value": "true"
            }
          ]
        },
        "sendBody": true,
        "contentType": "multipart-form-data",
        "bodyParameters": {
          "parameters": [
            {
              "name": "payload_json",
              "value": "={{ JSON.stringify($json.payload) }}"
            },
            {
              "parameterType": "formBinaryData",
              "name": "files[0]",
              "inputDataFieldName": "cover"
            }
          ]
        },
        "options": {
          "response": {
            "response": {
              "fullResponse": true
            }
          }
        }
      },
      "id": "76b8f9eb-4cae-44c3-af59-11966f747b77",
      "name": "To Discord, with the cover",
      "type": "n8n-nodes-base.httpRequest",
      "typeVersion": 4.2,
      "position": [
        720,
        -100
      ]
    },
    {
      "parameters": {
        "method": "POST",
        "url": "={{ $json.url }}",
        "sendQuery": true,
        "queryParameters": {
          "parameters": [
            {
              "name": "wait",
              "value": "true"
            }
          ]
        },
        "sendBody": true,
        "specifyBody": "json",
        "jsonBody": "={{ JSON.stringify($json.payload) }}",
        "options": {
          "response": {
            "response": {
              "fullResponse": true
            }
          }
        }
      },
      "id": "80ac9b8d-2d23-4745-a11e-0eac416b41b1",
      "name": "To Discord",
      "type": "n8n-nodes-base.httpRequest",
      "typeVersion": 4.2,
      "position": [
        720,
        100
      ]
    }
  ],
  "connections": {
    "From the console": {
      "main": [
        [
          {
            "node": "Sort by person",
            "type": "main",
            "index": 0
          }
        ]
      ]
    },
    "Sort by person": {
      "main": [
        [
          {
            "node": "Has a cover?",
            "type": "main",
            "index": 0
          }
        ]
      ]
    },
    "Has a cover?": {
      "main": [
        [
          {
            "node": "To Discord, with the cover",
            "type": "main",
            "index": 0
          }
        ],
        [
          {
            "node": "To Discord",
            "type": "main",
            "index": 0
          }
        ]
      ]
    }
  },
  "settings": {
    "executionOrder": "v1"
  }
}
```

## Other apps

Any app that takes a JSON POST at an `http://` address on your network and
answers quickly with a 2xx status should work: a Home Assistant webhook
trigger, Node-RED's HTTP In, or a script. Use `http://` at home. The app
decides everything else, the console only says what happened.

## What was tested

On 2026-11-15, with n8n 2.47.7 and Discord, on the reference console (a GEEKOM
A9 Max) and a second console as a LinkUp friend: setting the address; a game
started, paused, resumed and left; two accounts going to two channels; an
account taken off the list going nowhere; two achievements unlocked in one
game, each its own message; Sleep during a game, and the test message again
on waking; a LinkUp game from both sides, with the friend's Game events on
and off. Other apps were not tested.
