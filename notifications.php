<?php
// notifications.php - tiny push/poll hub for the ESP32 world clock.
// Upload to https://784488.xyz/notifications.php and change both keys below.
//
//   Push  (from any system):  ?a=push&key=WRITE_KEY&type=Teams&msg=Message+not+replied
//   Clear (all / by id/type): ?a=clear&key=WRITE_KEY            (&id=... or &type=...)
//   List  (debug):            ?a=list&key=WRITE_KEY
//   Poll  (the ESP32 uses):   ?a=poll&key=READ_KEY
//
// Optional push params: id (same id replaces the old item, so repeats don't pile up),
//                       ttl (seconds before it auto-expires, default 24h).

const WRITE_KEY   = 'bacon-blouse-aaa';
const READ_KEY    = 'decorate-sand-bbb';
const STORE       = __DIR__ . '/notif_store.php';   // starts with an exit line, so it can't be read from the web
const DEFAULT_TTL = 86400;
const MAX_ITEMS   = 50;

header('Content-Type: text/plain; charset=utf-8');
header('Cache-Control: no-store');

function out($code, $text) {
    http_response_code($code);
    echo $text;
    exit;
}

function clean($s, $max) {
    $s = preg_replace('/[^\x20-\x7E]/', '', (string)$s);   // the TFT fonts are ASCII only
    return substr(trim($s), 0, $max);
}

function load_items() {
    if (!is_file(STORE)) return [];
    $raw = file_get_contents(STORE);
    $nl  = strpos($raw, "\n");
    $data = $nl === false ? null : json_decode(substr($raw, $nl + 1), true);
    return is_array($data) ? $data : [];
}

function save_items($items) {
    $hdr = "<?php http_response_code(403); exit; ?>\n";
    file_put_contents(STORE, $hdr . json_encode(array_values($items)), LOCK_EX);
}

function live($items) {
    $now = time();
    return array_values(array_filter($items, function ($it) use ($now) { return $it['exp'] > $now; }));
}

// read-modify-write under an exclusive lock
function mutate($fn) {
    $lock = fopen(STORE . '.lock', 'c');
    flock($lock, LOCK_EX);
    $items = $fn(live(load_items()));
    save_items($items);
    flock($lock, LOCK_UN);
    fclose($lock);
}

$a   = isset($_REQUEST['a']) ? (string)$_REQUEST['a'] : '';
$key = isset($_REQUEST['key']) ? (string)$_REQUEST['key'] : '';
$isWrite = hash_equals(WRITE_KEY, $key);
$isRead  = $isWrite || hash_equals(READ_KEY, $key);

if ($a === 'poll') {
    if (!$isRead) out(403, 'forbidden');
    $items = live(load_items());
    echo count($items);
    if ($items) {
        $top = $items[0];
        echo "\n" . ($top['type'] !== '' ? $top['type'] . ': ' : '') . $top['msg'];
    }
    exit;
}

if (!$isWrite) out(403, 'forbidden');

if ($a === 'push') {
    $msg = clean(isset($_REQUEST['msg']) ? $_REQUEST['msg'] : '', 80);
    if ($msg === '') out(400, 'msg required');
    $type = clean(isset($_REQUEST['type']) ? $_REQUEST['type'] : '', 20);
    $id   = clean(isset($_REQUEST['id']) ? $_REQUEST['id'] : '', 40);
    if ($id === '') $id = md5($type . '|' . $msg);
    $ttl  = isset($_REQUEST['ttl']) ? (int)$_REQUEST['ttl'] : DEFAULT_TTL;
    $ttl  = max(60, min($ttl, 604800));

    mutate(function ($items) use ($id, $type, $msg, $ttl) {
        $items = array_filter($items, function ($it) use ($id) { return $it['id'] !== $id; });
        array_unshift($items, ['id' => $id, 'type' => $type, 'msg' => $msg, 'ts' => time(), 'exp' => time() + $ttl]);
        return array_slice($items, 0, MAX_ITEMS);
    });
    out(200, 'ok');
}

if ($a === 'clear') {
    $id   = clean(isset($_REQUEST['id']) ? $_REQUEST['id'] : '', 40);
    $type = clean(isset($_REQUEST['type']) ? $_REQUEST['type'] : '', 20);
    mutate(function ($items) use ($id, $type) {
        if ($id === '' && $type === '') return [];
        return array_filter($items, function ($it) use ($id, $type) {
            if ($id !== '' && $it['id'] === $id) return false;
            if ($type !== '' && $it['type'] === $type) return false;
            return true;
        });
    });
    out(200, 'ok');
}

if ($a === 'list') {
    header('Content-Type: application/json');
    echo json_encode(live(load_items()), JSON_PRETTY_PRINT);
    exit;
}

out(400, 'unknown action');
