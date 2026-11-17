# Game events

The console can tell one app in your home what is happening in a game: a game
started, paused or ended, an achievement unlocked. That app decides what
happens next: a post to your Discord, the lights dimming while a game runs, a
ping on your phone. The console only ever sends to that one address, and only
while it is on your own network. It never listens.

## What you need

An app on your home network that takes a webhook: [n8n](https://n8n.io),
Home Assistant, Node-RED, or a small script of your own. The
[examples](#examples) use n8n; [Build your own](#build-your-own) is for
everything else. The app can be on your home network or on your
[Tailscale](network.md#tailscale) network.

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

With an address saved, pressing the row again opens **Turn off** and
**Change address**. Turn off keeps the address, and nothing more is sent;
**Turn on** then needs no typing, and sends the test message first. Off is
the start for every console, and while it is off no part of it runs.

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

## Examples

These are starters, to give you an idea of what an app can do with the
messages. Each is a workflow file for n8n. In n8n, make a new workflow, open
its **⋯** menu, then **Import**, **From URL**, and paste the file's link
(right click the link here, **Copy link**). Or download the file and choose
**Import**, **From File**. If n8n then asks whether it came from an AI tool,
**Skip**.

Every one listens at `games`: on the console, type `http://` and your n8n's
address with `:5678/webhook/games` after it (see [Turn it on](#turn-it-on)).
The path is short on purpose: a webhook n8n makes itself gets a long random
path, which is hard to type with a controller. After any change in n8n,
**Save**, then **Publish**. In n8n 2, an edit only reaches the console after
you publish again, and a just-published workflow can answer 404 for a few
seconds. A workflow's **Executions** list shows each message the console sent
and what came of it. Each example was tried on the reference console with
n8n 2.47.7: Discord, Telegram, Philips Hue and the Discord status.

### Start here: game starts to Discord

[game-events-starter.json](game-events-starter.json) posts one line to a
Discord channel when anyone starts a game, with no code: three boxes.

1. **In Discord,** the gear beside a channel, **Integrations**,
   **Webhooks**, **New Webhook**, **Copy Webhook URL**.
2. **In n8n,** import the file, open **To Discord**, and paste the webhook
   over `https://discord.com/api/webhooks/PASTE-YOUR-WEBHOOK`. Save, then
   **Publish**.
3. **Start a game.** "🎮 Sam started Contra III: The Alien Wars (Super
   Nintendo)" appears in the channel, with your names.

The parts in `{{ }}` are filled in from each message: you type nothing there.
The file holds a sample message, Sam starting Contra III, which n8n uses when
you press **Execute workflow**, so you can try it without the console.

For each person's games in their own channel, import
[game-events-starter-for-two.json](game-events-starter-for-two.json) instead.
Its **Who is playing?** box has one rule per person, `Sam` and `Alex`: change
them to your people's names, exactly as the console shows them, and paste each
person's webhook in their own **To ... Discord** box. A third person is one
more rule and one more box. A person with no rule goes nowhere.

For a channel of achievements instead, change `game_started` in **Only game
starts** to `achievement_unlocked`, and the line in **To Discord** to
`🏆 {{ $json.body.person }} unlocked {{ $json.body.achievement }} in {{ $json.body.game }}`.

### Discord, with covers

[game-events-discord.json](game-events-discord.json) posts each person's
games to their own Discord channel: a card with the cover when a game starts,
a gold card when every achievement is unlocked, and one line for the rest. A
LinkUp game's card is purple and says "over LinkUp". A person not on its list
goes nowhere, so an account added to the console later stays quiet until you
choose where its games go.

1. **In Discord,** make a webhook for each person's channel, as above.
2. **In n8n,** import the file and open **Sort by person**. Change the two
   lines at the top of its code: each person's name exactly as the console
   shows it, then their webhook. Add a line for each person; delete the lines
   you do not need. Save, then **Publish**.

The `SEND` line under them lists the messages it posts: delete the ones you
do not want.

### Your Discord status

[game-events-discord-status.json](game-events-discord-status.json) shows the
game you are playing under your name in Discord, the way a computer game does.
Anyone who opens your profile sees the game, the system and a timer, or
"Paused" while the pause menu is open. It goes when you leave the game. If the
console loses power during a game, it goes when the console next starts, or
after 8 hours at most. It follows one person: whoever connects their Discord
in step 6.

Discord does not document this way of setting a status. It works today, but
Discord could change or stop it, and using what Discord does not document may
fall outside its developer terms, so Discord could act on the app or the
account that uses it. n8n never gets your Discord password: you allow it on
Discord's own page, and you can take that back in Discord's settings, under
**Authorized Apps**.

1. **Make a Discord app:** at
   [discord.com/developers/applications](https://discord.com/developers/applications),
   **New Application**. Any name: your status shows the game's name, not the
   app's.
2. **Turn on Discord's Social SDK for it:** open
   [the Social SDK's Getting Started page](https://discord.com/developers/applications/select/social-sdk/getting-started),
   choose your app, fill in the form, and **Submit**. Without it, step 6 fails
   with `invalid_scope`.
3. **Copy its keys:** on the app's **OAuth2** page, copy the **Client ID**,
   then **Reset Secret** and copy the **Client Secret**. Leave **Public
   Client** off.
4. **In n8n,** make a credential of the type **OAuth2 API**, named
   **Discord status**:
   - **Grant Type:** Authorization Code
   - **Authorization URL:** `https://discord.com/oauth2/authorize`
   - **Access Token URL:** `https://discord.com/api/oauth2/token`
   - **Client ID** and **Client Secret:** from step 3
   - **Scope:** `openid sdk.social_layer_presence`
   - **Authentication:** Body

   Copy its **OAuth Redirect URL**. It must start with the address you open
   n8n at. If it shows another one, such as `localhost` or an address nothing
   answers at, Discord will send you there after step 6 and the connection
   fails. Change the setting in n8n's container that holds that address
   (`WEBHOOK_URL`, `N8N_EDITOR_BASE_URL` or `N8N_HOST`) to n8n's own, restart
   n8n, and open the credential again.
5. **On the app's OAuth2 page,** **Add Redirect**, paste it, and **Save
   Changes**.
6. **In n8n,** in the credential, **Connect my account**, then **Authorize**
   on Discord's page. Discord's page also lists your friends: the workflow
   never reads them.
7. **Import the file,** open **What to tell Discord**, and fill in its first
   two lines: the Client ID from step 3, and your name exactly as the console
   shows it. Open **Show it on Discord** and **End the session**, and choose
   the **Discord status** credential in each. Save, then **Publish**.
8. **In Discord's settings,** under **Activity Privacy**, turn on sharing your
   activity.
9. **Start a game.** Your status shows it within a few seconds.

Discord drops a status like this after 20 minutes without word, so every 15
minutes, while a game runs, n8n tells Discord you are still playing. With no
game running, the clock does nothing.

**With another example:** the status has a clock of its own, so keep it as a
workflow of its own instead of pasting it into another, as [More than one at
once](#more-than-one-at-once) does. Change its **From the console** path to
`games-discord`. Then in the workflow the console sends to, copy this box,
click the canvas and paste, and drag a line from **From the console** to the
pasted **Hand on to Discord status**. Save, then **Publish** both. `localhost`
is n8n itself: if your n8n does not use port 5678, change it there.

```json
{"nodes":[{"parameters":{"method":"POST","url":"http://localhost:5678/webhook/games-discord","sendBody":true,"specifyBody":"json","jsonBody":"={{ JSON.stringify(Object.fromEntries(Object.entries($json.body ?? {}).filter(([key]) => key !== 'cover'))) }}","options":{"timeout":5000}},"name":"Hand on to Discord status","type":"n8n-nodes-base.httpRequest","typeVersion":4.2,"position":[240,-220],"onError":"continueRegularOutput"}],"connections":{}}
```

**A second person** needs a status of their own: import the file again, with
their name, a path of its own such as `games-discord-alex`, and a credential
of its own, connected while they are the one signed in to Discord in the
browser. The same Discord app serves both. Then paste a second hand-on box
with that path in its address.

### Telegram

[game-events-telegram.json](game-events-telegram.json) sends each person's
games to their own Telegram chat, from a bot of your own: the cover and the
game's name when a game starts or every achievement is unlocked, and one line
for the rest. A LinkUp game says "over LinkUp". Pause and resume arrive
without a sound, and so does everything between 10 pm and 7 am. A person not
on its list goes nowhere, and a child's name can go to a parent's chat.
Telegram shows every picture as wide as the chat, so a cover is large there.

1. **In Telegram,** open **@BotFather**, send `/newbot`, and give the bot a
   name and a username ending in `bot`. BotFather answers with its token.
2. **In n8n,** import the file, open **Sort by person**, and put the token in
   its first line. Save, then **Publish**.
3. **Each person** opens the bot in Telegram (search its username, or tap the
   `t.me` link BotFather gave) and presses **Start**.
4. **On the console,** type the address (with one already saved: **Change
   address**, then Done). The bot sends everyone who pressed Start their chat
   number.
5. **In n8n,** put each person's name, exactly as the console shows it, and
   their chat number in **Sort by person**. Save, then **Publish**.

The `SEND` line lists the messages it sends, and the quiet hours are the line
under it.

### Philips Hue

[game-events-hue.json](game-events-hue.json) makes the lights in one room or
zone follow the game. n8n talks to the Hue bridge on your home network, not
through Philips' cloud.

- **A game starts:** the lights that are on dim to a fifth, and each takes a
  colour from the game's cover. Lights that are off stay off, or come on for
  the game if you choose.
- **Pause:** they come up to 60%. **Resume:** back down to a fifth.
- **A LinkUp game:** purple instead of the cover's colours.
- **An achievement:** one blink. **Every achievement:** a rainbow for ten
  seconds.
- **The game ends:** every light goes back to exactly how it was.

1. **Find the bridge's address:** open
   [discovery.meethue.com](https://discovery.meethue.com) in a browser at
   home. It lists your bridge, and its address is the `internalipaddress`.
2. **In n8n,** import the file, open **Lights follow the game**, and fill in
   its first two lines: the bridge's address, and the room or zone, named
   exactly as in the Hue app. Save, then **Publish**.
3. **Press the round button on the bridge, then start a game within 30
   seconds.** The bridge gives n8n a key of its own, and the lights dim. This
   is needed only once.

The lines below those two change how it looks, and the code says what each
takes: the colour while a game runs (the cover's colours, one colour of your
choice, or the lights' own), a colour of their own for some people, the
LinkUp colour, a rainbow or a colour flash for every achievement, how bright
the lights are while playing and paused (0 turns them off), and whether
lights that are off come on for the game. If the
lights do not change, the workflow's **Executions** list says why.

### More than one at once

The console sends to one address, and only one workflow can answer it. To
post to Discord and work the lights, for example: open the second file's
link, select all of it and copy it, then click the first workflow's canvas
and paste. Delete the pasted **From the console** box, and drag a line from
the first one's **From the console** to the box the deleted one led to. Save,
then **Publish**. The Discord status is the exception: [Your Discord
status](#your-discord-status) says how to add it.

## Build your own

Any app that can answer a web request works: Home Assistant's webhook
trigger, Node-RED's HTTP In, or a script. What it has to do:

- Take an HTTP POST at an `http://` address on your home network or your
  Tailscale network. Use `http://` at home.
- Read the JSON body: [The messages](#the-messages) has every field.
- Answer with any 2xx status within five seconds, and do anything slow after
  answering. A message not answered in time is dropped.
- Answer the `test` message. It comes first on every network, and until your
  app answers it, nothing else is sent.
- Expect one message at a time, in order, and nothing kept for later while
  your app is down.
- Pass over events and fields it does not know: new ones can be added.

To try your app without the console, send it a pretend game start from a
computer on your network, with your app's address at the end:

```sh
curl -X POST -H "Content-Type: application/json" -d '{"format":1,"event":"game_started","time":"2026-11-15T19:32:00-05:00","person":"Sam","game":"Contra III: The Alien Wars","system":"Super Nintendo","rom_id":1234,"linkup":"unused","linkup_game":"none"}' http://your-app-address
```

The app decides everything else: the console only says what happened.

An AI assistant can help write one: give it this page's address and say what
should happen, and it has every message and the six examples to work from.
