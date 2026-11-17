# Game events: your Discord status

Shows the game you are playing under your name in Discord, from a
workflow to import into n8n.

[Game events](game-events.md) says how to turn it on on the console, and
how to import an example into n8n.

[game-events-discord-status.json](game-events-discord-status.json) shows the
game you are playing under your name in Discord, the way a computer game does.
Anyone who opens your profile sees the game, the system and a timer, or
"Paused" while the pause menu is open. It goes when you leave the game. If the
console loses power during a game, it goes when the console next starts, or
after 8 hours at most. It follows one person: whoever connects their Discord
in step 6.

## Before you start

Discord does not document this way of setting a status. It works today, but
Discord could change or stop it, and using what Discord does not document may
fall outside its developer terms, so Discord could act on the app or the
account that uses it. n8n never gets your Discord password: you allow it on
Discord's own page, and you can take that back in Discord's settings, under
**Authorized Apps**.

## Set it up

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

## With another example

The status has a clock of its own, so keep it as a workflow of its own
instead of pasting it into another, as [More than one at
once](game-events.md#more-than-one-at-once) does. Change its **From the
console** path to `games-discord`. Then open
[game-events-hand-on.json](game-events-hand-on.json), select all of it and
copy it, click the canvas of the workflow the console sends to, and paste.
Drag a line from its **From the console** to the pasted **Hand on to Discord
status**. Save, then **Publish** both. `localhost` in the box is n8n itself:
if your n8n does not use port 5678, change it there.

## A second person

A second person needs a status of their own: import the file again, with
their name, a path of its own such as `games-discord-alex`, and a credential
of its own, connected while they are the one signed in to Discord in the
browser. The same Discord app serves both. Then paste a second hand-on box
with that path in its address.
