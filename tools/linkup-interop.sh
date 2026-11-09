#!/usr/bin/env bash
#
# The console's LinkUp code against a real LinkUp server: every part of PROTOCOL.md a console
# speaks, with no stream and no screen, on two fresh linked containers it starts and removes.
# linkup/noise-test.cpp holds the bytes to the server's own code; this holds the conversation.
#
#   tools/linkup-interop.sh <linkup-probe> <standin> <image>
#
# Run on any Linux machine with podman. <linkup-probe> is `make linkup-probe` in linkup/,
# <standin> is cmd/standin from MMagTech/cabinet-linkup, <image> its container image. It uses
# ports 17845 to 17859, 17945 to 17959 and 17998 to 18000 on this machine and nothing else.
set -uo pipefail

probe=$1
standin=$2
image=$3
work=$(mktemp -d)
A=127.0.0.1:17845
B=127.0.0.1:17945
AA=http://127.0.0.1:17846
AB=http://127.0.0.1:17946
fakes=()
failed=0

cleanup() {
    for pid in "${fakes[@]:-}"; do [ -n "$pid" ] && kill "$pid" 2>/dev/null; done
    podman rm -fv linkup-i-a linkup-i-b >/dev/null 2>&1
    rm -rf "$work"
}
trap cleanup EXIT

say() { printf '%s\n' "$*"; }
fail() { say "FAILED: $1"; failed=$((failed + 1)); }
pass() { say "ok: $1"; }
# want <what> <expected first word> <command...>
want() {
    local what=$1 expected=$2
    shift 2
    local out
    out=$("$@" 2>&1)
    if [ "${out%% *}" = "$expected" ]; then pass "$what"; else fail "$what: wanted $expected, got: $out"; fi
    LAST=$out
}

start() { # name console admin relay range
    podman run -d --name "linkup-i-$1" --network=host --read-only --cap-drop=ALL --security-opt=no-new-privileges \
        -e "LINKUP_CONSOLE_PORT=$2" -e "LINKUP_ADMIN_PORT=$3" -e "LINKUP_RELAY_PORT=$4" -e "LINKUP_RELAY_PORTS=$5" \
        "$image" >/dev/null || exit 2
}
podman rm -fv linkup-i-a linkup-i-b >/dev/null 2>&1
start a 17845 17846 17847 17850-17859
start b 17945 17946 17947 17950-17959
for url in "$AA" "$AB"; do
    for _ in $(seq 1 20); do curl -fs -o /dev/null "$url/static/mark.svg" && break; sleep 1; done
done

page() { # jar url path [form...]
    local jar=$1 url=$2 path=$3
    shift 3
    curl -s -o "$work/out.html" -D "$work/headers" -b "$jar" -c "$jar" "$@" "$url$path"
}
follow() { # jar url
    local where
    where=$(sed -n 's/^[Ll]ocation: *//p' "$work/headers" | tr -d '\r')
    page "$1" "$2" "$where"
}
codeOnPage() { grep -o '[A-Z0-9]\{4\}-[A-Z0-9]\{4\}-[A-Z0-9]\{4\}' "$work/out.html" | head -1; }
setup() { # jar url name address
    page "$1" "$2" /setup/password -d password=correct-horse -d again=correct-horse
    page "$1" "$2" /setup/address -d "name=$3" --data-urlencode "address=$4"
    page "$1" "$2" /setup/relay/skip
}
own() { # jar url address
    page "$1" "$2" /setup/own --data-urlencode "address=$3"
    follow "$1" "$2"
    codeOnPage
}
people() { # jar url: a person's id by their option on the invite form
    page "$1" "$2" /people/add
    grep -o '<option value="[0-9a-f]*"[^>]*>[^<]*' "$work/out.html"
}
inviteFor() { # jar url person
    page "$1" "$2" /people/add -d "for=$3" -d note=
    follow "$1" "$2"
    codeOnPage
}
field() { sed -n "s/.*\"$1\":\"\\([^\"]*\\)\".*/\\1/p" | head -1; }

setup "$work/a.jar" "$AA" "Riley's" "$A"
setup "$work/b.jar" "$AB" "Morgan's" "$B"

say "== pairing"
code=$(own "$work/a.jar" "$AA" "$A")
want "a wrong code" wrong_code "$probe" "$work/wrong" pair "$A" "K7MR-4QXT-9HPV" a alex
want "a code of the wrong length" wrong_code "$probe" "$work/wrong" pair "$A" "K7MR" a alex
want "an address with nothing there" unreachable "$probe" "$work/wrong" pair 127.0.0.1:17998 "$code" a alex
[ -e "$work/wrong/server.json" ] && fail "a failed pairing kept a server"
want "the owner's console pairs" paired "$probe" "$work/alex" pair "$A" "$code" acct-alex alex
want "that code again" wrong_code "$probe" "$work/again" pair "$A" "$code" acct-other other
stat -c '%a %n' "$work/alex" "$work/alex/key" | while read -r mode name; do
    case "$mode" in 700|600) ;; *) fail "$name is mode $mode" ;; esac
done

say "== a fake server"
for mode in msg2 redirect huge; do
    "$probe" "$work/fake" fake 17999 "$mode" &
    fakes+=($!)
    sleep 0.3
    case "$mode" in
        msg2) word=not_that_server ;;
        *) word=unreachable ;;
    esac
    want "fake server: $mode" "$word" "$probe" "$work/victim" pair 127.0.0.1:17999 "K7MR-4QXT-9HPV" a alex
    [ -e "$work/victim/server.json" ] && fail "fake server: $mode: a server was kept"
    kill "${fakes[-1]}" 2>/dev/null; wait "${fakes[-1]}" 2>/dev/null
done
fakeKey=$("$probe" "$work/fake" key)
for mode in console-behind server-behind removed; do
    "$probe" "$work/fake" fake 17999 "$mode" &
    fakes+=($!)
    sleep 0.3
    rm -rf "$work/old"
    "$probe" "$work/old" remember 127.0.0.1:17999 "$fakeKey"
    case "$mode" in
        console-behind) word=console_behind ;;
        server-behind) word=server_behind ;;
        removed) word=removed ;;
    esac
    want "a server that says: $mode" "$word" "$probe" "$work/old" op '{"op":"config"}'
    kill "${fakes[-1]}" 2>/dev/null; wait "${fakes[-1]}" 2>/dev/null
done
# A server without the remembered key cannot answer an Open.
"$probe" "$work/fake" fake 17999 removed &
fakes+=($!)
sleep 0.3
rm -rf "$work/old"
"$probe" "$work/old" remember 127.0.0.1:17999 "$("$probe" "$work/alex" key)"
want "a server with the wrong key" unreachable "$probe" "$work/old" op '{"op":"config"}'
kill "${fakes[-1]}" 2>/dev/null; wait "${fakes[-1]}" 2>/dev/null

say "== a session"
want "signs in" ok "$probe" "$work/alex" op '{"op":"signin","account":"acct-alex","username":"alex","status":"online"}'
want "the server's name and STUN" ok "$probe" "$work/alex" op '{"op":"config"}'
case "$LAST" in *'"name":"Riley'*'"stun":['*) pass "config holds them" ;; *) fail "config: $LAST" ;; esac
# A clock that moved back: the last t is ahead of now, and the next is one more.
echo $(( $(date +%s) + 100000 ))000000000 > "$work/alex/last-t"
want "a clock that moved back still opens" ok "$probe" "$work/alex" op '{"op":"config"}'
behind=$(cat "$work/alex/last-t")
want "and again, one t on" ok "$probe" "$work/alex" op '{"op":"config"}'
[ "$(cat "$work/alex/last-t")" = "$((behind + 1))" ] || fail "t did not go up by one"
# An old t is a replay, and the server's rule holds: this is what keeping it on disk prevents.
cp "$work/alex/last-t" "$work/t-kept"
echo 1000 > "$work/alex/last-t"
want "a t lower than the server's highest is refused" unreachable "$probe" "$work/alex" op '{"op":"config"}'
cp "$work/t-kept" "$work/alex/last-t"
want "a retyped address needs no new pairing" ok "$probe" "$work/alex" address "localhost:17845"
want "an address with http in it" ok "$probe" "$work/alex" address "http://$A"
want "an address with a slash after it" ok "$probe" "$work/alex" address "$A/"
want "a bad address fails" unreachable "$probe" "$work/alex" address "127.0.0.1:17998"
"$probe" "$work/alex" address "$A" >/dev/null

say "== friends"
alexId=$(people "$work/a.jar" "$AA" | grep -m1 alex | sed 's/.*value="//; s/".*//')
[ -n "$alexId" ] || fail "alex is not on the owner's page"
code=$(inviteFor "$work/a.jar" "$AA" "$alexId")
want "a friend's console pairs" paired "$probe" "$work/maya" pair "$A" "$code" acct-maya maya
want "the friend signs in" ok "$probe" "$work/maya" op '{"op":"signin","account":"acct-maya","username":"maya","status":"online"}'
want "alex hears the list" ok "$probe" "$work/alex" op '{"op":"friends"}'
mayaId=$(printf '%s' "$LAST" | field id)
case "$LAST" in *'"username":"maya"'*'"status":"online"'*) pass "maya is online on alex's list" ;; *) fail "the list: $LAST" ;; esac
want "maya hears the list" ok "$probe" "$work/maya" op '{"op":"friends"}'
alexFriend=$(printf '%s' "$LAST" | field id)

say "== an invite, sealed"
invite=$(od -An -N16 -tx1 /dev/urandom | tr -d ' \n')
sealed=$("$probe" "$work/alex" seal "$mayaId" "$invite" invite "Contra III")
want "the knock" ok "$probe" "$work/alex" op "{\"op\":\"invite\",\"invite\":\"$invite\",\"friend\":\"$mayaId\",\"sealed\":\"$sealed\"}"
want "the same knock again, as after a lost answer" ok "$probe" "$work/alex" op "{\"op\":\"invite\",\"invite\":\"$invite\",\"friend\":\"$mayaId\",\"sealed\":\"$sealed\"}"
case "$LAST" in *'"error"'*) fail "the retry: $LAST" ;; esac
want "maya's list shows it" ok "$probe" "$work/maya" op '{"op":"friends"}'
boxed=$(printf '%s' "$LAST" | sed -n 's/.*"sealed":"\([^"]*\)".*/\1/p')
opened=$("$probe" "$work/maya" open "$alexFriend" "$invite" "$boxed")
case "$opened" in *'"game":"Contra III"'*) pass "maya opens the box, and the game's name is in it" ;; *) fail "the box: $opened" ;; esac
want "bound to its invite" refused "$probe" "$work/maya" open "$alexFriend" "${invite%?}0" "$boxed"
if docker_log=$(podman logs linkup-i-a 2>&1) && printf '%s' "$docker_log" | grep -q "Contra"; then fail "the game's name is in the server's log"; fi

want "the server's events reach maya" ok "$probe" "$work/maya" op '{"op":"wait","after":0}'
case "$LAST" in *'"type":"invite"'*) pass "an invite event" ;; *) fail "the events: $LAST" ;; esac
want "maya joins" ok "$probe" "$work/maya" op "{\"op\":\"join\",\"invite\":\"$invite\"}"
session=$(printf '%s' "$LAST" | field session)
want "the same join again" ok "$probe" "$work/maya" op "{\"op\":\"join\",\"invite\":\"$invite\"}"
want "alex hears it" ok "$probe" "$work/alex" op '{"op":"wait","after":0}'
case "$LAST" in *'"type":"joined"'*) pass "a joined event" ;; *) fail "the events: $LAST" ;; esac
signal=$("$probe" "$work/alex" seal "$mayaId" "$invite" signal '{"sdp":"made up"}')
want "connection details" ok "$probe" "$work/alex" op "{\"op\":\"signal\",\"session\":\"$session\",\"sealed\":\"$signal\"}"
want "they reach maya" ok "$probe" "$work/maya" op '{"op":"wait","after":0}'
case "$LAST" in *'"type":"signal"'*) pass "a signal event" ;; *) fail "the events: $LAST" ;; esac
want "no relay on this server" ok "$probe" "$work/alex" op "{\"op\":\"relay\",\"session\":\"$session\"}"
case "$LAST" in *'"relay":null'*) pass "says so" ;; *) fail "the relay: $LAST" ;; esac
want "how it connected" ok "$probe" "$work/alex" op "{\"op\":\"report\",\"session\":\"$session\",\"path\":\"direct\",\"reason\":\"none\"}"
want "the session ends" ok "$probe" "$work/alex" op "{\"op\":\"end\",\"session\":\"$session\"}"
want "maya hears it" ok "$probe" "$work/maya" op '{"op":"wait","after":0}'
case "$LAST" in *'"type":"session_ended"'*) pass "a session_ended event" ;; *) fail "the events: $LAST" ;; esac

say "== a linked server"
"$standin" pair "$work/sam" "$B" "$(own "$work/b.jar" "$AB" "$B")" acct-sam sam >/dev/null || fail "sam pairs"
"$standin" signin "$work/sam" acct-sam sam >/dev/null
samId=$(people "$work/b.jar" "$AB" | grep -m1 sam | sed 's/.*value="//; s/".*//')
page "$work/a.jar" "$AA" /links/make -d "for=$alexId"
follow "$work/a.jar" "$AA"
code=$(codeOnPage)
page "$work/b.jar" "$AB" /links/enter --data-urlencode "address=$A" -d "code=$code" -d "for=$samId"
grep -qi '^location: /links' "$work/headers" || fail "linking the servers"
for _ in $(seq 1 30); do
    "$probe" "$work/alex" op '{"op":"friends"}' | grep -q '"username":"sam".*"status":"online"\|"status":"online".*"username":"sam"' && break
    "$standin" friends "$work/sam" >/dev/null
    sleep 1
done
want "alex's list across the link" ok "$probe" "$work/alex" op '{"op":"friends"}'
case "$LAST" in *'"username":"sam"'*) pass "sam is on it" ;; *) fail "the list: $LAST" ;; esac
case "$LAST" in *'"server":"Morgan'*) pass "with sam's server's name" ;; *) fail "the list: $LAST" ;; esac

say "== taken off the server"
page "$work/a.jar" "$AA" /people
last=
grep -o 'people?person=[^"]*' "$work/out.html" | sed 's/.*=//' | sort -u > "$work/people"
while read -r person; do
    page "$work/a.jar" "$AA" "/people?person=$person"
    if grep -q 'detail-title">maya<' "$work/out.html"; then
        last=$(grep -o '/consoles/remove?id=[0-9]*' "$work/out.html" | sed 's/.*id=//' | head -1)
        break
    fi
done < "$work/people"
if [ -n "$last" ]; then
    page "$work/a.jar" "$AA" /consoles/remove -d "id=$last"
    want "a removed console hears it, and only from the server" removed "$probe" "$work/maya" op '{"op":"friends"}'
else
    fail "no console to remove on the owner's page"
fi
say "== a restart"
# The server keeps who is signed in on each console in memory only. After a restart it says so in one
# word, and the console signs in again on it (conductor.cpp, signInAgain).
podman restart linkup-i-a >/dev/null || fail "restarting the server"
for _ in $(seq 1 20); do curl -fs -o /dev/null "$AA/static/mark.svg" && break; sleep 1; done
want "the list after a restart" ok "$probe" "$work/alex" op '{"op":"friends"}'
case "$LAST" in *'"error":"no_account"'*) pass "the server says nobody is signed in" ;; *) fail "after a restart: $LAST" ;; esac
want "signs in again" ok "$probe" "$work/alex" op '{"op":"signin","account":"acct-alex","username":"alex","status":"online"}'
want "and the list comes" ok "$probe" "$work/alex" op '{"op":"friends"}'
case "$LAST" in *'"friends":['*) pass "a list" ;; *) fail "the list: $LAST" ;; esac

say "== leaving"
want "alex leaves" ok "$probe" "$work/alex" op '{"op":"leave"}'
want "and is gone" unreachable "$probe" "$work/alex" op '{"op":"config"}'

log=$(podman logs linkup-i-a 2>&1; podman logs linkup-i-b 2>&1)
for word in maya "$code" "$fakeKey"; do
    if printf '%s' "$log" | grep -q "$word"; then fail "$word is in a server's log"; fi
done
if [ "$failed" -gt 0 ]; then
    say "$failed failed"
    exit 1
fi
say "all good"
