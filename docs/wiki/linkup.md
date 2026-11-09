# LinkUp

Play a game with a friend on another CabinetOS console, from their house. You host the game on your console,
and it streams to theirs. They play as player two and only you need the game.

It works like going to a friend's house. You knock, maybe they can't play today, and you go home. Two friends
have usually arranged it already, by phone or text, so the console chases nobody: no alerts when a friend comes
online, no reminders, and no invites kept for later. There is no voice chat. Use your phone.

LinkUp is off until you turn it on. Off means its program is not running, nothing listens and nothing is
contacted.

## What you need

- A CabinetOS console each, on a RomM server. It can be your own or one someone shares with you, and it does not
  have to be the same one.
- One LinkUp server between you, which either of you can run. It is a small free program, and the project runs no
  service. Settings, **Streaming**, **Server**, **Run your own** shows a QR code to how to set one up.

## Join a server

Whoever runs the server makes a code on its admin page and picks whose friend it is for, then sends you its
address and the code.

1. Settings, **Streaming**, **LinkUp** on (the PIN, if one is set).
2. Press **Server**, then **Add a server**. Type the address, then the code.
3. LinkUp joins the top bar. Whoever was signed in when you typed the code becomes the friend.

A code works once and lasts 7 days. Everyone on your console can use LinkUp after that, and each person has their
own friends: a child sees only their own, and only the server's owner can add one.

![The LinkUp tab before anyone has added you as a friend](../media/linkup-no-friends.webp)

## Play

1. Open **LinkUp** in the top bar. Press an **Online** friend, then a game. Games a friend cannot play are not on
   the list: Wii games that need a Remote, light-gun games, handhelds and Steam.

   ![The LinkUp tab with friends Online, Offline, in a game, and one who invited you](../media/linkup-friends.webp)

2. Your console waits on *Waiting for Alex* for up to 30 minutes. **B** cancels. If the game still has to download,
   it starts now.

   ![Waiting for Alex to join Contra III](../media/linkup-waiting.webp)

3. Your friend sees a pill, *Invite from Alex*, and their list says *Invited: Contra III*. They press it, then
   **Join** or **Decline**.
4. The game starts on both when they join, so you both begin at its title screen. You are player one, they are
   player two, and any other controller on your console does nothing in the game until the session ends.

**Status** at the top of the list is whether your friends see you as Online. It stays where you leave it. You also
show Offline after 15 minutes with no press, when the screen is off, while the console sleeps and in Steam.

## While you play

- **Pausing stops the game for both of you.** Your friend pauses as in any game (both sticks pressed): *Paused*,
  with **Resume** and **Leave**, and you see *Alex paused*. **A** on either console resumes both.
- **Your friend sees only the game.** When you open a menu, or anything else is on your screen, no picture or sound
  is sent, and they see *Waiting for Alex*.
- **Saves, states and achievements are yours.** Nothing from the game is kept on your friend's console.
- **If the connection drops,** the game pauses on both with *Reconnecting to Alex*. After a minute, *Alex left*, and
  your game carries on as a normal game.
- **If you leave the game,** the session stays open: you land on the game list with *Playing with Alex*, and the
  next game you pick starts on both with no new invite. **B** there ends the session.
- **After 10 minutes with no press from either of you,** the game pauses on both. After 15 more, the session ends,
  and your game stays paused on your TV.

![Alex paused the game, and A resumes it](../media/linkup-paused.webp)

## What your friend can see

Everything is encrypted, and the server never learns what you play. But a game that connects directly shows each
console the other's internet address, as any direct connection does. LinkUp is only for people a server's owner
chose, and that is one more reason.

## If it won't connect

*Can't connect to the LinkUp server* means the server cannot be reached. A game already running carries on. *Can't
connect to Alex* means the two consoles found no way through: the server's owner can turn on its relay, which sends
the game through the server and uses the owner's upload. Nothing is built or promised for WireGuard or Tailscale,
though a network you already share is treated as any other.

## Run your own server

The setup guide is in the server's repository, [cabinet-linkup](https://github.com/MMagTech/cabinet-linkup). On the
console, Settings, **Streaming**, **Server**, **Run your own** shows a QR code to it.
