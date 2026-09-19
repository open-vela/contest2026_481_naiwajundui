from flask import Flask, request, jsonify, render_template_string, redirect, url_for, send_file, Response
from flask_socketio import SocketIO
import os, json, csv, io, sqlite3, logging, time, random
from datetime import datetime, timedelta

app = Flask(__name__)
socketio = SocketIO(app, cors_allowed_origins='*')

DB_FILE = 'velatime.db'
WALLPAPER_DIR = 'static/wallpapers'
LOG_FILE = 'velatime.log'
os.makedirs(WALLPAPER_DIR, exist_ok=True)

logging.basicConfig(filename=LOG_FILE, level=logging.INFO,
    format='%(asctime)s [%(levelname)s] %(message)s', encoding='utf-8')


def init_db():
    conn = sqlite3.connect(DB_FILE)
    c = conn.cursor()
    c.execute('''CREATE TABLE IF NOT EXISTS records (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        time TEXT, device_id TEXT, heart_rate INTEGER, steps INTEGER, raw TEXT)''')
    c.execute('''CREATE TABLE IF NOT EXISTS config (key TEXT PRIMARY KEY, value TEXT)''')
    c.execute('''CREATE TABLE IF NOT EXISTS device_names (device_id TEXT PRIMARY KEY, name TEXT)''')
    c.execute('''CREATE TABLE IF NOT EXISTS online (device_id TEXT PRIMARY KEY, last_seen REAL)''')
    c.execute('''CREATE TABLE IF NOT EXISTS schedule (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        course TEXT, weekday INTEGER, start TEXT, end TEXT, location TEXT, note TEXT DEFAULT '')''')
    c.execute('''CREATE TABLE IF NOT EXISTS messages (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        content TEXT, time TEXT, read INTEGER DEFAULT 0)''')
    c.execute('''CREATE TABLE IF NOT EXISTS todos (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        content TEXT, done INTEGER DEFAULT 0, time TEXT)''')
    try:
        c.execute("ALTER TABLE schedule ADD COLUMN note TEXT DEFAULT ''")
    except sqlite3.OperationalError:
        pass
    conn.commit()
    conn.close()

init_db()


def get_config(key, default=''):
    conn = sqlite3.connect(DB_FILE)
    c = conn.cursor()
    c.execute('SELECT value FROM config WHERE key=?', (key,))
    row = c.fetchone()
    conn.close()
    return row[0] if row else default


def set_config(key, value):
    conn = sqlite3.connect(DB_FILE)
    c = conn.cursor()
    c.execute('INSERT OR REPLACE INTO config (key, value) VALUES (?, ?)', (key, str(value)))
    conn.commit()
    conn.close()


def get_device_names():
    conn = sqlite3.connect(DB_FILE)
    c = conn.cursor()
    c.execute('SELECT device_id, name FROM device_names')
    rows = c.fetchall()
    conn.close()
    return {r[0]: r[1] for r in rows}


def update_online(device_id):
    conn = sqlite3.connect(DB_FILE)
    c = conn.cursor()
    c.execute('INSERT OR REPLACE INTO online (device_id, last_seen) VALUES (?, ?)', (device_id, time.time()))
    conn.commit()
    conn.close()


def get_online_devices(threshold=30):
    conn = sqlite3.connect(DB_FILE)
    c = conn.cursor()
    c.execute('SELECT device_id, last_seen FROM online')
    rows = c.fetchall()
    conn.close()
    now = time.time()
    return [r[0] for r in rows if now - r[1] < threshold]


def get_today_schedule():
    weekday = datetime.now().isoweekday()
    conn = sqlite3.connect(DB_FILE)
    c = conn.cursor()
    c.execute('SELECT id, course, start, end, location, note FROM schedule WHERE weekday=? ORDER BY start', (weekday,))
    rows = c.fetchall()
    conn.close()
    return [{'id': r[0], 'course': r[1], 'start': r[2], 'end': r[3], 'location': r[4], 'note': r[5]} for r in rows]


def get_week_schedule():
    conn = sqlite3.connect(DB_FILE)
    c = conn.cursor()
    c.execute('SELECT id, course, weekday, start, end, location, note FROM schedule ORDER BY weekday, start')
    rows = c.fetchall()
    conn.close()
    week = {i: [] for i in range(1, 8)}
    for r in rows:
        week[r[2]].append({'id': r[0], 'course': r[1], 'start': r[3], 'end': r[4], 'location': r[5], 'note': r[6]})
    return week


def get_next_course():
    now = datetime.now()
    today = get_today_schedule()
    for course in today:
        ct = datetime.strptime(course['start'], '%H:%M').replace(year=now.year, month=now.month, day=now.day)
        if ct > now:
            delta = (ct - now).total_seconds() / 60
            return {**course, 'minutes_left': int(delta)}
    return None


def get_free_slots():
    today = get_today_schedule()
    if not today:
        return [{'start': '08:00', 'end': '22:00'}]
    slots = []
    day_start = datetime.strptime('08:00', '%H:%M')
    day_end = datetime.strptime('22:00', '%H:%M')
    prev_end = day_start
    for c in today:
        cs = datetime.strptime(c['start'], '%H:%M')
        ce = datetime.strptime(c['end'], '%H:%M')
        if cs > prev_end:
            slots.append({'start': prev_end.strftime('%H:%M'), 'end': cs.strftime('%H:%M')})
        if ce > prev_end:
            prev_end = ce
    if prev_end < day_end:
        slots.append({'start': prev_end.strftime('%H:%M'), 'end': day_end.strftime('%H:%M')})
    return slots


def get_hourly_hr():
    week_ago = (datetime.now() - timedelta(days=7)).strftime('%Y-%m-%d')
    conn = sqlite3.connect(DB_FILE)
    c = conn.cursor()
    c.execute('SELECT time, heart_rate FROM records WHERE time >= ?', (week_ago,))
    rows = c.fetchall()
    conn.close()
    hourly = {h: [] for h in range(24)}
    for t, hr in rows:
        if hr:
            try:
                hour = int(t[11:13])
                hourly[hour].append(hr)
            except:
                pass
    return {h: round(sum(v) / len(v), 1) if v else 0 for h, v in hourly.items()}


def get_daily_steps():
    conn = sqlite3.connect(DB_FILE)
    c = conn.cursor()
    c.execute('''SELECT substr(time,1,10) as d, SUM(steps) FROM records
                 WHERE time >= date('now', '-30 days') GROUP BY d''')
    rows = c.fetchall()
    conn.close()
    return {r[0]: r[1] or 0 for r in rows}


def get_messages():
    conn = sqlite3.connect(DB_FILE)
    c = conn.cursor()
    c.execute('SELECT id, content, time FROM messages ORDER BY id DESC LIMIT 20')
    rows = c.fetchall()
    conn.close()
    return [{'id': r[0], 'content': r[1], 'time': r[2]} for r in rows]


def get_todos():
    conn = sqlite3.connect(DB_FILE)
    c = conn.cursor()
    c.execute('SELECT id, content, done, time FROM todos ORDER BY id DESC LIMIT 50')
    rows = c.fetchall()
    conn.close()
    return [{'id': r[0], 'content': r[1], 'done': r[2], 'time': r[3]} for r in rows]


def get_device_comparison():
    conn = sqlite3.connect(DB_FILE)
    c = conn.cursor()
    c.execute('SELECT DISTINCT device_id FROM records')
    devices = [r[0] for r in c.fetchall()]
    result = {}
    for d in devices:
        c.execute('SELECT time, heart_rate, steps FROM records WHERE device_id=? ORDER BY id DESC LIMIT 20', (d,))
        rows = c.fetchall()
        result[d] = [{'time': r[0], 'heart_rate': r[1], 'steps': r[2]} for r in rows][::-1]
    conn.close()
    return result


@app.route('/')
def index():
    return render_template_string(PAGE_HTML,
        quote=get_config('daily_quote', '今天也辛苦了'),
        countdown=get_config('countdown', ''),
        countdown_note=get_config('countdown_note', ''),
        wallpapers=os.listdir(WALLPAPER_DIR),
        current_wallpaper=get_config('current_wallpaper', ''),
        device_names=json.dumps(get_device_names(), ensure_ascii=False),
        today_schedule=get_today_schedule(),
        week_schedule=get_week_schedule(),
        free_slots=get_free_slots(),
        messages=get_messages(),
        todos=get_todos(),
        daily_goal=get_config('daily_goal', '8000'),
        theme=get_config('theme', 'light'),
        phrases=json.loads(get_config('phrases', '["今天也辛苦了","加油！"]'))
    )


_rate_limit = {}
def check_rate_limit(device_id, max_per_sec=1):
    now = time.time()
    if device_id not in _rate_limit:
        _rate_limit[device_id] = []
    _rate_limit[device_id] = [t for t in _rate_limit[device_id] if now - t < 1]
    if len(_rate_limit[device_id]) >= max_per_sec:
        return False
    _rate_limit[device_id].append(now)
    return True


@app.route('/api/upload', methods=['POST'])
def upload():
    try:
        payload = request.get_json()
        device_id = payload.get('device_id', '默认')
        if not check_rate_limit(device_id):
            return jsonify({'status': 'error', 'msg': '请求过于频繁'}), 429
        payload['time'] = datetime.now().strftime('%Y-%m-%d %H:%M:%S')
        conn = sqlite3.connect(DB_FILE)
        c = conn.cursor()
        c.execute('INSERT INTO records (time, device_id, heart_rate, steps, raw) VALUES (?, ?, ?, ?, ?)',
                  (payload['time'], device_id, payload.get('heart_rate'), payload.get('steps'), json.dumps(payload)))
        conn.commit()
        conn.close()
        update_online(device_id)
        socketio.emit('new_data', payload)
        return jsonify({'status': 'ok'}), 200
    except Exception as e:
        return jsonify({'status': 'error', 'msg': str(e)}), 500


@app.route('/api/records')
def records():
    limit = int(request.args.get('limit', 100))
    conn = sqlite3.connect(DB_FILE)
    c = conn.cursor()
    c.execute('SELECT time, device_id, heart_rate, steps FROM records ORDER BY id DESC LIMIT ?', (limit,))
    rows = c.fetchall()
    c.execute('SELECT COUNT(*) FROM records')
    total = c.fetchone()[0]
    conn.close()
    return jsonify({'total': total, 'records': [{'time': r[0], 'device_id': r[1], 'heart_rate': r[2], 'steps': r[3]} for r in rows][::-1]})


@app.route('/api/stats')
def stats():
    now = datetime.now()
    today = now.strftime('%Y-%m-%d')
    week_ago = (now - timedelta(days=7)).strftime('%Y-%m-%d')
    conn = sqlite3.connect(DB_FILE)
    c = conn.cursor()
    def q(w):
        c.execute(f'SELECT SUM(steps), AVG(heart_rate) FROM records WHERE {w}')
        r = c.fetchone()
        return r[0] or 0, round(r[1], 1) if r[1] else ''
    ts, th = q(f"time LIKE '{today}%'")
    ws, wh = q(f"time >= '{week_ago}'")
    conn.close()
    return jsonify({'today_steps': ts, 'week_steps': ws, 'today_hr': th, 'week_hr': wh})


@app.route('/api/hourly_hr')
def hourly_hr():
    return jsonify(get_hourly_hr())


@app.route('/api/daily_steps')
def daily_steps():
    return jsonify(get_daily_steps())


@app.route('/api/device_comparison')
def device_comparison():
    return jsonify(get_device_comparison())


@app.route('/api/daily')
def daily():
    countdown_days = ''
    cd = get_config('countdown', '')
    if cd:
        target = datetime.strptime(cd, '%Y-%m-%d')
        countdown_days = (target - datetime.now()).days
    return jsonify({
        'quote': get_config('daily_quote', '今天也辛苦了'),
        'countdown': countdown_days,
        'countdown_note': get_config('countdown_note', ''),
        'wallpapers': os.listdir(WALLPAPER_DIR),
        'current_wallpaper': get_config('current_wallpaper', ''),
        'online_devices': get_online_devices(),
        'daily_goal': int(get_config('daily_goal', '8000')),
        'theme': get_config('theme', 'light'),
        'phrases': json.loads(get_config('phrases', '[]'))
    })


@app.route('/api/schedule')
def schedule_api():
    return jsonify({
        'today': get_today_schedule(),
        'next': get_next_course(),
        'free_slots': get_free_slots(),
        'week': get_week_schedule()
    })


@app.route('/api/messages')
def messages_api():
    return jsonify({'messages': get_messages()})


@app.route('/api/todos')
def todos_api():
    return jsonify({'todos': get_todos()})


@app.route('/api/ping', methods=['POST'])
def ping():
    update_online((request.get_json() or {}).get('device_id', '默认'))
    return jsonify({'status': 'ok'})


@app.route('/add_course', methods=['POST'])
def add_course():
    conn = sqlite3.connect(DB_FILE)
    c = conn.cursor()
    c.execute('INSERT INTO schedule (course, weekday, start, end, location, note) VALUES (?, ?, ?, ?, ?, ?)',
              (request.form['course'], int(request.form['weekday']), request.form['start'],
               request.form['end'], request.form.get('location', ''), request.form.get('note', '')))
    conn.commit()
    conn.close()
    return redirect(url_for('index'))


@app.route('/delete_course/<int:cid>', methods=['POST'])
def delete_course(cid):
    conn = sqlite3.connect(DB_FILE)
    c = conn.cursor()
    c.execute('DELETE FROM schedule WHERE id=?', (cid,))
    conn.commit()
    conn.close()
    return redirect(url_for('index'))


@app.route('/upload_schedule', methods=['POST'])
def upload_schedule():
    file = request.files.get('schedule')
    if not file:
        return redirect(url_for('index'))
    content = file.read().decode('utf-8-sig')
    reader = csv.DictReader(io.StringIO(content))
    conn = sqlite3.connect(DB_FILE)
    c = conn.cursor()
    c.execute('DELETE FROM schedule')
    for row in reader:
        try:
            c.execute('INSERT INTO schedule (course, weekday, start, end, location, note) VALUES (?, ?, ?, ?, ?, ?)',
                      (row['课程名'], int(row['星期']), row['开始时间'], row['结束时间'],
                       row.get('地点', ''), row.get('备注', '')))
        except Exception as e:
            logging.error(f'课表解析失败: {e}')
    conn.commit()
    conn.close()
    return redirect(url_for('index'))


@app.route('/download_template')
def download_template():
    content = '课程名,星期,开始时间,结束时间,地点,备注\n高等数学,1,08:00,09:40,教三-201,带课本\n'
    return Response(content.encode('utf-8-sig'), mimetype='text/csv',
        headers={'Content-Disposition': 'attachment; filename=schedule_template.csv'})


@app.route('/set_quote', methods=['POST'])
def set_quote():
    set_config('daily_quote', request.form['quote'])
    return redirect(url_for('index'))


@app.route('/set_countdown', methods=['POST'])
def set_countdown():
    set_config('countdown', request.form['date'])
    set_config('countdown_note', request.form.get('note', ''))
    return redirect(url_for('index'))


@app.route('/upload_wallpaper', methods=['POST'])
def upload_wallpaper():
    f = request.files['wallpaper']
    if f: f.save(os.path.join(WALLPAPER_DIR, f.filename))
    return redirect(url_for('index'))


@app.route('/delete_wallpaper/<name>', methods=['POST'])
def delete_wallpaper(name):
    p = os.path.join(WALLPAPER_DIR, name)
    if os.path.exists(p): os.remove(p)
    if get_config('current_wallpaper', '') == name:
        set_config('current_wallpaper', '')
    return redirect(url_for('index'))


@app.route('/set_current_wallpaper/<name>', methods=['POST'])
def set_current_wallpaper(name):
    set_config('current_wallpaper', name)
    return redirect(url_for('index'))


@app.route('/rename_device', methods=['POST'])
def rename_device():
    conn = sqlite3.connect(DB_FILE)
    c = conn.cursor()
    c.execute('INSERT OR REPLACE INTO device_names (device_id, name) VALUES (?, ?)',
              (request.form['old'], request.form['new']))
    conn.commit()
    conn.close()
    return redirect(url_for('index'))


@app.route('/push_message', methods=['POST'])
def push_message():
    conn = sqlite3.connect(DB_FILE)
    c = conn.cursor()
    c.execute('INSERT INTO messages (content, time) VALUES (?, ?)',
              (request.form['content'], datetime.now().strftime('%Y-%m-%d %H:%M:%S')))
    conn.commit()
    conn.close()
    return redirect(url_for('index'))


@app.route('/delete_message/<int:mid>', methods=['POST'])
def delete_message(mid):
    conn = sqlite3.connect(DB_FILE)
    c = conn.cursor()
    c.execute('DELETE FROM messages WHERE id=?', (mid,))
    conn.commit()
    conn.close()
    return redirect(url_for('index'))


@app.route('/add_todo', methods=['POST'])
def add_todo():
    conn = sqlite3.connect(DB_FILE)
    c = conn.cursor()
    c.execute('INSERT INTO todos (content, time) VALUES (?, ?)',
              (request.form['content'], datetime.now().strftime('%Y-%m-%d %H:%M:%S')))
    conn.commit()
    conn.close()
    return redirect(url_for('index'))


@app.route('/toggle_todo/<int:tid>', methods=['POST'])
def toggle_todo(tid):
    conn = sqlite3.connect(DB_FILE)
    c = conn.cursor()
    c.execute('UPDATE todos SET done = 1 - done WHERE id=?', (tid,))
    conn.commit()
    conn.close()
    return redirect(url_for('index'))


@app.route('/delete_todo/<int:tid>', methods=['POST'])
def delete_todo(tid):
    conn = sqlite3.connect(DB_FILE)
    c = conn.cursor()
    c.execute('DELETE FROM todos WHERE id=?', (tid,))
    conn.commit()
    conn.close()
    return redirect(url_for('index'))


@app.route('/set_theme', methods=['POST'])
def set_theme():
    set_config('theme', request.form['theme'])
    return redirect(url_for('index'))


@app.route('/set_daily_goal', methods=['POST'])
def set_daily_goal():
    set_config('daily_goal', request.form['goal'])
    return redirect(url_for('index'))


@app.route('/add_phrase', methods=['POST'])
def add_phrase():
    phrases = json.loads(get_config('phrases', '[]'))
    phrases.append(request.form['phrase'])
    set_config('phrases', json.dumps(phrases, ensure_ascii=False))
    return redirect(url_for('index'))


@app.route('/delete_phrase/<int:idx>', methods=['POST'])
def delete_phrase(idx):
    phrases = json.loads(get_config('phrases', '[]'))
    if 0 <= idx < len(phrases): phrases.pop(idx)
    set_config('phrases', json.dumps(phrases, ensure_ascii=False))
    return redirect(url_for('index'))


@app.route('/generate_mock', methods=['POST'])
def generate_mock():
    conn = sqlite3.connect(DB_FILE)
    c = conn.cursor()
    now = datetime.now()
    for i in range(100):
        t = (now - timedelta(minutes=i * 10)).strftime('%Y-%m-%d %H:%M:%S')
        c.execute('INSERT INTO records (time, device_id, heart_rate, steps, raw) VALUES (?, ?, ?, ?, ?)',
                  (t, 'watch-mock', random.randint(60, 120), random.randint(0, 300), '{}'))
    conn.commit()
    conn.close()
    return redirect(url_for('index'))


@app.route('/export_excel')
def export_excel():
    from openpyxl import Workbook
    conn = sqlite3.connect(DB_FILE)
    c = conn.cursor()
    c.execute('SELECT time, device_id, heart_rate, steps FROM records')
    rows = c.fetchall()
    conn.close()
    wb = Workbook(); ws = wb.active
    ws.append(['时间', '设备', '心率', '步数'])
    for r in rows: ws.append(list(r))
    out = io.BytesIO(); wb.save(out); out.seek(0)
    return send_file(out, mimetype='application/vnd.openxmlformats-officedocument.spreadsheetml.sheet',
                     as_attachment=True, download_name='velatime_data.xlsx')


@app.route('/backup')
def backup():
    return send_file(DB_FILE, as_attachment=True, download_name='velatime_backup.db')


@app.route('/restore', methods=['POST'])
def restore():
    f = request.files.get('db')
    if f:
        f.save(DB_FILE); init_db()
    return redirect(url_for('index'))


@app.route('/clear', methods=['POST'])
def clear():
    conn = sqlite3.connect(DB_FILE)
    c = conn.cursor()
    c.execute('DELETE FROM records')
    conn.commit(); conn.close()
    return jsonify({'status': 'ok'})


@app.route('/export')
def export():
    conn = sqlite3.connect(DB_FILE)
    c = conn.cursor()
    c.execute('SELECT time, device_id, heart_rate, steps FROM records')
    rows = c.fetchall(); conn.close()
    data = {'records': [{'time': r[0], 'device_id': r[1], 'heart_rate': r[2], 'steps': r[3]} for r in rows]}
    return Response(json.dumps(data, ensure_ascii=False, indent=2), mimetype='application/json',
        headers={'Content-Disposition': 'attachment; filename=velatime_data.json'})


@app.route('/export_csv')
def export_csv():
    conn = sqlite3.connect(DB_FILE)
    c = conn.cursor()
    c.execute('SELECT time, device_id, heart_rate, steps FROM records')
    rows = c.fetchall(); conn.close()
    out = io.StringIO(); w = csv.writer(out)
    w.writerow(['时间', '设备', '心率', '步数']); w.writerows(rows)
    return Response(out.getvalue().encode('utf-8-sig'), mimetype='text/csv',
        headers={'Content-Disposition': 'attachment; filename=velatime_data.csv'})


@app.errorhandler(404)
def nf(e): return jsonify({'status': 'error', 'msg': '接口不存在'}), 404
@app.errorhandler(500)
def se(e): return jsonify({'status': 'error', 'msg': '服务器内部错误'}), 500
PAGE_HTML = '''
<!DOCTYPE html>
<html lang="zh">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>VelaTime 数据看板</title>
<script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
<script src="https://cdn.socket.io/4.7.2/socket.io.min.js"></script>
<style>
:root { --bg: #f0f2f8; --card: #fff; --text: #1a1a2e; --muted: #8a8aa3; --border: #e4e6f0; --primary: #6366f1; --danger: #ef4444; --success: #10b981; --shadow: 0 4px 20px rgba(99,102,241,0.08); }
body.dark { --bg: #0f0f1a; --card: #1a1a2e; --text: #f0f0f5; --muted: #6b6b8a; --border: #2a2a40; --shadow: 0 4px 20px rgba(0,0,0,0.3); }
* { box-sizing: border-box; margin: 0; padding: 0; }
body { font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", "PingFang SC", sans-serif; max-width: 1100px; margin: 0 auto; padding: 32px 20px 60px; background: var(--bg); color: var(--text); line-height: 1.6; transition: background 0.3s, color 0.3s; }
.header { display: flex; justify-content: space-between; align-items: center; margin-bottom: 28px; }
.header h1 { font-size: 26px; font-weight: 700; background: linear-gradient(135deg, #6366f1, #a855f7); -webkit-background-clip: text; -webkit-text-fill-color: transparent; }
.theme-btn { background: var(--card); border: 1px solid var(--border); width: 42px; height: 42px; border-radius: 50%; font-size: 20px; cursor: pointer; color: var(--text); transition: all 0.2s; }
.theme-btn:hover { transform: rotate(20deg); }
.grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(300px, 1fr)); gap: 20px; }
.card { background: var(--card); border-radius: 18px; padding: 24px; box-shadow: var(--shadow); border: 1px solid var(--border); transition: all 0.3s; }
.card:hover { transform: translateY(-2px); box-shadow: 0 8px 30px rgba(99,102,241,0.15); }
.card.full { grid-column: 1 / -1; }
.card h2 { font-size: 15px; font-weight: 600; color: var(--muted); text-transform: uppercase; letter-spacing: 0.5px; margin-bottom: 16px; }
.stat-grid { display: grid; grid-template-columns: repeat(4, 1fr); gap: 12px; }
.stat { text-align: center; padding: 16px 8px; background: linear-gradient(135deg, rgba(99,102,241,0.06), rgba(168,85,247,0.06)); border-radius: 14px; }
.stat .num { font-size: 26px; font-weight: 700; color: var(--primary); display: block; margin-bottom: 4px; }
.stat .label { font-size: 12px; color: var(--muted); }
input, textarea, select { width: 100%; padding: 12px 14px; margin-bottom: 10px; border: 1px solid var(--border); border-radius: 10px; font-size: 14px; background: var(--bg); color: var(--text); }
input:focus, select:focus { outline: none; border-color: var(--primary); }
button { padding: 12px 20px; background: linear-gradient(135deg, #6366f1, #8b5cf6); color: white; border: none; border-radius: 10px; cursor: pointer; font-size: 14px; font-weight: 600; transition: all 0.2s; }
button:hover { transform: translateY(-1px); box-shadow: 0 6px 16px rgba(99,102,241,0.3); }
.btn-danger { background: linear-gradient(135deg, #ef4444, #dc2626); }
.btn-small { padding: 6px 12px; font-size: 12px; }
.table-wrap { overflow-x: auto; }
table { width: 100%; border-collapse: collapse; font-size: 13px; }
th { text-align: left; padding: 10px 12px; color: var(--muted); font-weight: 600; font-size: 12px; text-transform: uppercase; border-bottom: 1px solid var(--border); }
td { padding: 12px; border-bottom: 1px solid var(--border); }
.device-tag { display: inline-flex; align-items: center; gap: 6px; background: var(--bg); padding: 6px 12px; border-radius: 20px; margin: 4px; font-size: 13px; border: 1px solid var(--border); }
.device-tag.online { border-color: var(--success); background: rgba(16,185,129,0.1); }
.device-tag.offline { opacity: 0.5; }
.wallpaper-list { display: flex; flex-wrap: wrap; gap: 12px; }
.wallpaper-item { text-align: center; }
.wallpaper-item img { width: 110px; height: 110px; object-fit: cover; border-radius: 12px; cursor: pointer; border: 2px solid transparent; }
.wallpaper-item.current img { border-color: var(--primary); }
.countdown-box { background: linear-gradient(135deg, rgba(99,102,241,0.1), rgba(168,85,247,0.1)); border-radius: 14px; padding: 20px; text-align: center; }
.countdown-days { font-size: 48px; font-weight: 800; color: var(--primary); line-height: 1; }
.countdown-days.warn { color: var(--danger); }
.countdown-note { font-size: 14px; color: var(--muted); margin-top: 8px; }
.toast { position: fixed; top: 24px; right: 24px; background: var(--card); color: var(--text); padding: 14px 22px; border-radius: 12px; box-shadow: 0 10px 30px rgba(0,0,0,0.15); border-left: 4px solid var(--primary); opacity: 0; transform: translateX(120px); transition: all 0.3s; z-index: 999; font-weight: 500; }
.toast.show { opacity: 1; transform: translateX(0); }
.toast.remind { border-left-color: #f59e0b; background: #fffbeb; color: #92400e; }
.modal { display: none; position: fixed; inset: 0; background: rgba(0,0,0,0.85); z-index: 1000; justify-content: center; align-items: center; }
.modal.show { display: flex; }
.modal img { max-width: 90%; max-height: 90%; border-radius: 16px; }
.timeline { position: relative; padding-left: 24px; }
.timeline::before { content: ''; position: absolute; left: 6px; top: 8px; bottom: 8px; width: 2px; background: var(--border); }
.course-item { position: relative; padding: 12px 0 12px 16px; }
.course-item::before { content: ''; position: absolute; left: -22px; top: 18px; width: 12px; height: 12px; border-radius: 50%; background: var(--primary); border: 3px solid var(--card); box-shadow: 0 0 0 2px var(--primary); }
.course-time { font-size: 13px; color: var(--muted); font-weight: 600; }
.course-name { font-size: 16px; font-weight: 600; margin: 2px 0; }
.course-location { font-size: 13px; color: var(--muted); }
.course-note { font-size: 12px; color: #f59e0b; margin-top: 4px; }
.empty-hint { text-align: center; color: var(--muted); padding: 20px; font-size: 14px; }
.slot { display: inline-block; background: rgba(16,185,129,0.1); color: var(--success); padding: 6px 12px; border-radius: 20px; margin: 4px; font-size: 13px; border: 1px solid var(--success); }
.week-grid { display: grid; grid-template-columns: repeat(7, 1fr); gap: 8px; }
.week-day { background: var(--bg); border-radius: 10px; padding: 10px; min-height: 150px; font-size: 12px; }
.week-day h4 { font-size: 12px; color: var(--muted); margin-bottom: 8px; text-align: center; }
.week-course { background: rgba(99,102,241,0.15); border-radius: 6px; padding: 4px 6px; margin-bottom: 4px; font-size: 11px; border-left: 3px solid var(--primary); }
.week-course .t { color: var(--muted); font-size: 10px; }
.edit-form { background: var(--bg); padding: 12px; border-radius: 10px; margin-top: 12px; display: none; }
.edit-form.show { display: block; }
.gauge-wrap { text-align: center; padding: 10px; }
.gauge-value { font-size: 36px; font-weight: 800; color: var(--primary); }
.gauge-unit { font-size: 14px; color: var(--muted); }
.calendar { display: grid; grid-template-columns: repeat(7, 1fr); gap: 4px; }
.cal-cell { width: 100%; aspect-ratio: 1; border-radius: 4px; background: var(--border); position: relative; }
.cal-cell[data-level="1"] { background: #c7d2fe; }
.cal-cell[data-level="2"] { background: #818cf8; }
.cal-cell[data-level="3"] { background: #6366f1; }
.cal-cell[data-level="4"] { background: #4338ca; }
.cal-cell:hover::after { content: attr(data-tip); position: absolute; bottom: 100%; left: 50%; transform: translateX(-50%); background: #333; color: #fff; padding: 4px 8px; border-radius: 4px; font-size: 11px; white-space: nowrap; z-index: 10; }
@media (max-width: 600px) {
  body { padding: 20px 14px 40px; }
  .header h1 { font-size: 20px; }
  .stat-grid { grid-template-columns: repeat(2, 1fr); }
  button { width: 100%; }
  .btn-small { width: auto; }
  .week-grid { grid-template-columns: repeat(2, 1fr); }
}
</style>
</head>
<body>
<div class="header">
  <h1>VelaTime 数据看板</h1>
  <button class="theme-btn" onclick="toggleTheme()">🌙</button>
</div>
<div id="toast" class="toast"></div>
<div id="modal" class="modal" onclick="this.classList.remove('show')"><img id="modal-img"></div>

<div class="grid">

  <div class="card full">
    <h2>实时数据</h2>
    <canvas id="chart" height="70"></canvas>
  </div>

  <div class="card full">
    <h2>今日 / 本周统计</h2>
    <div class="stat-grid">
      <div class="stat"><span class="num" id="today-steps">0</span><span class="label">今日步数</span></div>
      <div class="stat"><span class="num" id="week-steps">0</span><span class="label">本周步数</span></div>
      <div class="stat"><span class="num" id="today-hr">-</span><span class="label">今日平均心率</span></div>
      <div class="stat"><span class="num" id="week-hr">-</span><span class="label">本周平均心率</span></div>
    </div>
  </div>

  <div class="card">
    <h2>❤️ 实时心率仪表盘</h2>
    <div class="gauge-wrap">
      <canvas id="gauge" width="180" height="180"></canvas>
      <div class="gauge-value" id="gauge-value">-</div>
      <div class="gauge-unit">次/分</div>
    </div>
  </div>

  <div class="card">
    <h2>🔥 心率热力图（按小时）</h2>
    <canvas id="heatmap" height="150"></canvas>
  </div>

  <div class="card full">
    <h2>📆 步数日历（最近 30 天）</h2>
    <div class="calendar" id="calendar"></div>
  </div>

  <div class="card full">
    <h2>📊 多设备对比</h2>
    <canvas id="compareChart" height="100"></canvas>
  </div>

  <div class="card full">
    <h2>📅 今日课表</h2>
    <div style="display:flex;gap:10px;margin-bottom:12px;flex-wrap:wrap;">
      <button class="btn-small" onclick="toggleWeek()" id="weekToggleBtn">📅 切换到整周</button>
      <button class="btn-small" onclick="toggleEditForm()">➕ 添加课程</button>
    </div>

    <div id="todayView">
      {% if today_schedule %}
        <div class="timeline">
          {% for c in today_schedule %}
            <div class="course-item">
              <div class="course-time">{{ c.start }} - {{ c.end }}</div>
              <div class="course-name">{{ c.course }}</div>
              <div class="course-location">📍 {{ c.location }}</div>
              {% if c.note %}<div class="course-note">📝 {{ c.note }}</div>{% endif %}
              <div style="margin-top:6px;">
                <form method="POST" action="/delete_course/{{ c.id }}" style="display:inline;">
                  <button class="btn-small btn-danger" type="submit">删除</button>
                </form>
              </div>
            </div>
          {% endfor %}
        </div>
      {% else %}
        <div class="empty-hint">今天没有课 📭</div>
      {% endif %}

      <div style="margin-top:16px;">
        <h2>🕐 空档时间</h2>
        {% for s in free_slots %}
          <span class="slot">{{ s.start }} - {{ s.end }}</span>
        {% endfor %}
      </div>
    </div>

    <div id="weekView" style="display:none;">
      <div class="week-grid">
        {% for wd in range(1, 8) %}
          <div class="week-day">
            <h4>{{ ['周一','周二','周三','周四','周五','周六','周日'][wd-1] }}</h4>
            {% for c in week_schedule[wd] %}
              <div class="week-course">
                <div class="t">{{ c.start }}</div>
                <div>{{ c.course }}</div>
              </div>
            {% endfor %}
          </div>
        {% endfor %}
      </div>
    </div>

    <div class="edit-form" id="editForm">
      <h3 style="margin-bottom:10px;font-size:14px;">添加课程</h3>
      <form method="POST" action="/add_course">
        <input type="text" name="course" placeholder="课程名" required>
        <select name="weekday" required>
          <option value="1">周一</option><option value="2">周二</option>
          <option value="3">周三</option><option value="4">周四</option>
          <option value="5">周五</option><option value="6">周六</option><option value="7">周日</option>
        </select>
        <input type="text" name="start" placeholder="开始时间 08:00" required>
        <input type="text" name="end" placeholder="结束时间 09:40" required>
        <input type="text" name="location" placeholder="地点">
        <input type="text" name="note" placeholder="备注（如：带实验报告）">
        <button type="submit">保存</button>
      </form>
    </div>

    <div style="margin-top:16px;padding-top:16px;border-top:1px solid var(--border);">
      <h3 style="font-size:14px;margin-bottom:8px;">批量导入</h3>
      <form method="POST" action="/upload_schedule" enctype="multipart/form-data">
        <input type="file" name="schedule" accept=".csv">
        <div style="display:flex;gap:10px;flex-wrap:wrap;">
          <button type="submit" style="flex:1;">上传 CSV</button>
          <a href="/download_template" style="flex:1;"><button type="button" style="width:100%;">下载模板</button></a>
        </div>
      </form>
    </div>
  </div>

  <div class="card full">
    <h2>⚙️ 数据工具</h2>
    <div style="display:flex;gap:10px;flex-wrap:wrap;">
      <form method="POST" action="/generate_mock" style="flex:1;"><button type="submit">🎲 生成模拟数据</button></form>
      <a href="/export_excel" style="flex:1;"><button type="button" style="width:100%;">📊 导出 Excel</button></a>
      <a href="/backup" style="flex:1;"><button type="button" style="width:100%;">💾 备份数据库</button></a>
    </div>
  </div>

  <div class="card">
    <h2>📨 消息推送</h2>
    <form method="POST" action="/push_message">
      <input type="text" name="content" placeholder="推送到表盘的消息" required>
      <button type="submit">推送</button>
    </form>
    <div style="margin-top:12px;max-height:200px;overflow-y:auto;">
      {% for m in messages %}
        <div style="display:flex;justify-content:space-between;padding:6px 0;border-bottom:1px solid var(--border);">
          <span style="font-size:13px;">{{ m.content }}</span>
          <form method="POST" action="/delete_message/{{ m.id }}" style="display:inline;">
            <button class="btn-small btn-danger" type="submit">×</button>
          </form>
        </div>
      {% endfor %}
    </div>
  </div>

  <div class="card">
    <h2>✅ 待办事项</h2>
    <form method="POST" action="/add_todo">
      <input type="text" name="content" placeholder="添加待办" required>
      <button type="submit">添加</button>
    </form>
    <div style="margin-top:12px;max-height:200px;overflow-y:auto;">
      {% for t in todos %}
        <div style="display:flex;justify-content:space-between;padding:6px 0;border-bottom:1px solid var(--border);">
          <span style="font-size:13px;{{ 'text-decoration:line-through;color:var(--muted);' if t.done else '' }}">{{ t.content }}</span>
          <div>
            <form method="POST" action="/toggle_todo/{{ t.id }}" style="display:inline;"><button class="btn-small" type="submit">{{ '↩' if t.done else '✓' }}</button></form>
            <form method="POST" action="/delete_todo/{{ t.id }}" style="display:inline;"><button class="btn-small btn-danger" type="submit">×</button></form>
          </div>
        </div>
      {% endfor %}
    </div>
  </div>

  <div class="card">
    <h2>💬 快捷短语</h2>
    <form method="POST" action="/add_phrase">
      <input type="text" name="phrase" placeholder="添加短语" required>
      <button type="submit">添加</button>
    </form>
    <div style="margin-top:12px;">
      {% for p in phrases %}
        <span style="display:inline-block;background:var(--bg);padding:6px 12px;border-radius:20px;margin:4px;font-size:13px;border:1px solid var(--border);">
          {{ p }}
          <form method="POST" action="/delete_phrase/{{ loop.index0 }}" style="display:inline;"><button class="btn-small btn-danger" style="padding:2px 6px;" type="submit">×</button></form>
        </span>
      {% endfor %}
    </div>
  </div>

  <div class="card">
    <h2>🎨 主题 & 目标</h2>
    <form method="POST" action="/set_theme">
      <label style="font-size:13px;color:var(--muted);">表盘主题</label>
      <select name="theme">
        <option value="light" {{ 'selected' if theme=='light' else '' }}>明亮</option>
        <option value="dark" {{ 'selected' if theme=='dark' else '' }}>暗黑</option>
        <option value="festival" {{ 'selected' if theme=='festival' else '' }}>节日</option>
      </select>
      <button type="submit">保存主题</button>
    </form>
    <form method="POST" action="/set_daily_goal" style="margin-top:12px;">
      <label style="font-size:13px;color:var(--muted);">每日步数目标</label>
      <input type="number" name="goal" value="{{ daily_goal }}" required>
      <button type="submit">保存目标</button>
    </form>
  </div>

  <div class="card">
    <h2>倒计时</h2>
    <div class="countdown-box">
      <div class="countdown-days {{ 'warn' if countdown and countdown|int < 3 else '' }}">{{ countdown or '—' }}</div>
      {% if countdown_note %}<div class="countdown-note">📌 {{ countdown_note }}</div>{% endif %}
    </div>
    <form method="POST" action="/set_countdown" style="margin-top:16px;">
      <input type="date" name="date" required>
      <input type="text" name="note" placeholder="备注" value="{{ countdown_note }}">
      <button type="submit">设置</button>
    </form>
  </div>

  <div class="card">
    <h2>每日一句</h2>
    <p style="font-size:18px;font-weight:600;margin-bottom:16px;">"{{ quote }}"</p>
    <form method="POST" action="/set_quote">
      <input type="text" name="quote" placeholder="输入一句话" required>
      <button type="submit">保存</button>
    </form>
  </div>

  <div class="card full">
    <h2>数据记录</h2>
    <div style="display:flex;gap:12px;margin-bottom:16px;flex-wrap:wrap;">
      <select id="filter-device" onchange="renderRecords()" style="margin:0;flex:1;min-width:150px;"><option value="">全部设备</option></select>
      <input type="date" id="filter-date" onchange="renderRecords()" style="margin:0;flex:1;min-width:150px;">
    </div>
    <div class="table-wrap">
      <table id="records"><thead><tr><th>时间</th><th>设备</th><th>心率</th><th>步数</th></tr></thead><tbody></tbody></table>
    </div>
    <div style="display:flex;gap:10px;flex-wrap:wrap;margin-top:16px;">
      <a href="/export" download><button>导出 JSON</button></a>
      <a href="/export_csv" download><button>导出 CSV</button></a>
      <button class="btn-danger" onclick="clearData()">清空所有记录</button>
    </div>
  </div>

  <div class="card full">
    <h2>已连接设备</h2>
    <div id="devices"></div>
  </div>

  <div class="card full">
    <h2>壁纸管理</h2>
    <form method="POST" action="/upload_wallpaper" enctype="multipart/form-data">
      <input type="file" name="wallpaper" accept="image/*" required>
      <button type="submit">上传壁纸</button>
    </form>
    <div class="wallpaper-list">
      {% for w in wallpapers %}
      <div class="wallpaper-item {{ 'current' if w == current_wallpaper else '' }}">
        <img src="/static/wallpapers/{{ w }}" onclick="showModal('/static/wallpapers/{{ w }}')">
        <div>
          <form method="POST" action="/set_current_wallpaper/{{ w }}" style="display:inline;"><button class="btn-small" type="submit">设为当前</button></form>
          <form method="POST" action="/delete_wallpaper/{{ w }}" style="display:inline;"><button class="btn-small btn-danger" type="submit">删除</button></form>
        </div>
      </div>
      {% endfor %}
    </div>
  </div>

</div>

<script>
let allRecords = [];
const deviceNames = {{ device_names|safe }};
const dailyGoal = {{ daily_goal }};
const socket = io();

function toggleTheme() {
  document.body.classList.toggle('dark');
  localStorage.setItem('theme', document.body.classList.contains('dark') ? 'dark' : 'light');
}
if (localStorage.getItem('theme') === 'dark') document.body.classList.add('dark');

function showToast(msg, remind) {
  const t = document.getElementById('toast');
  t.innerText = (remind ? '⏰ ' : '🔔 ') + msg;
  t.className = 'toast show' + (remind ? ' remind' : '');
  setTimeout(() => t.classList.remove('show'), 3500);
}

function showModal(src) {
  document.getElementById('modal-img').src = src;
  document.getElementById('modal').classList.add('show');
}

function toggleWeek() {
  const today = document.getElementById('todayView');
  const week = document.getElementById('weekView');
  const btn = document.getElementById('weekToggleBtn');
  if (week.style.display === 'none') {
    week.style.display = 'block'; today.style.display = 'none';
    btn.innerText = '📆 切换到今天';
  } else {
    week.style.display = 'none'; today.style.display = 'block';
    btn.innerText = '📅 切换到整周';
  }
}

function toggleEditForm() {
  document.getElementById('editForm').classList.toggle('show');
}

let lastRemindMin = -1;
async function checkReminder() {
  try {
    const res = await fetch('/api/schedule').then(r => r.json());
    if (res.next && res.next.minutes_left <= 15 && res.next.minutes_left > 0) {
      if (res.next.minutes_left !== lastRemindMin) {
        lastRemindMin = res.next.minutes_left;
        showToast(`下一节「${res.next.course}」还有 ${res.next.minutes_left} 分钟`, true);
      }
    }
  } catch(e) {}
}
checkReminder();
setInterval(checkReminder, 30000);

let gaugeChart = null;
function renderGauge(value) {
  if (gaugeChart) gaugeChart.destroy();
  const ctx = document.getElementById('gauge').getContext('2d');
  gaugeChart = new Chart(ctx, {
    type: 'doughnut',
    data: { datasets: [{ data: [value || 0, Math.max(0, 200 - (value || 0))], backgroundColor: ['#ef4444', '#e0e0e0'], borderWidth: 0 }] },
    options: { cutout: '75%', rotation: -90, circumference: 180, plugins: { legend: { display: false }, tooltip: { enabled: false } } }
  });
  document.getElementById('gauge-value').innerText = value || '-';
}

let heatmapChart = null;
async function renderHeatmap() {
  const data = await fetch('/api/hourly_hr').then(r => r.json());
  const labels = Array.from({length: 24}, (_, i) => i + ':00');
  const values = labels.map((_, i) => data[i] || 0);
  const ctx = document.getElementById('heatmap').getContext('2d');
  if (heatmapChart) heatmapChart.destroy();
  heatmapChart = new Chart(ctx, {
    type: 'bar',
    data: { labels, datasets: [{ label: '平均心率', data: values, backgroundColor: values.map(v =>
      v === 0 ? '#e0e0e0' : v < 60 ? '#93c5fd' : v <= 100 ? '#10b981' : '#ef4444') }] },
    options: { plugins: { legend: { display: false } }, scales: {
      x: { ticks: { color: getComputedStyle(document.body).getPropertyValue('--muted'), font: { size: 10 } } },
      y: { ticks: { color: getComputedStyle(document.body).getPropertyValue('--muted') } }
    } }
  });
}

async function renderCalendar() {
  const data = await fetch('/api/daily_steps').then(r => r.json());
  const today = new Date();
  const cells = [];
  for (let i = 29; i >= 0; i--) {
    const d = new Date(today); d.setDate(d.getDate() - i);
    const key = d.toISOString().slice(0, 10);
    const steps = data[key] || 0;
    const level = steps === 0 ? 0 : steps < 3000 ? 1 : steps < 6000 ? 2 : steps < 10000 ? 3 : 4;
    cells.push(`<div class="cal-cell" data-level="${level}" data-tip="${key}: ${steps} 步"></div>`);
  }
  document.getElementById('calendar').innerHTML = cells.join('');
}

let compareChart = null;
async function renderCompare() {
  const data = await fetch('/api/device_comparison').then(r => r.json());
  const colors = ['#ef4444', '#6366f1', '#10b981', '#f59e0b', '#8b5cf6'];
  const datasets = Object.keys(data).map((d, i) => ({
    label: deviceNames[d] || d,
    data: data[d].map(r => r.heart_rate || 0),
    borderColor: colors[i % colors.length],
    fill: false, tension: 0.4, borderWidth: 2, pointRadius: 0
  }));
  const labels = Object.values(data)[0]?.map(r => r.time.slice(-8)) || [];
  const ctx = document.getElementById('compareChart').getContext('2d');
  if (compareChart) compareChart.destroy();
  compareChart = new Chart(ctx, {
    type: 'line',
    data: { labels, datasets },
    options: { plugins: { legend: { labels: { color: getComputedStyle(document.body).getPropertyValue('--text') } } },
      scales: { x: { ticks: { color: getComputedStyle(document.body).getPropertyValue('--muted') } },
                y: { ticks: { color: getComputedStyle(document.body).getPropertyValue('--muted') } } } }
  });
}

function renderRecords() {
  const df = document.getElementById('filter-device').value;
  const dt = document.getElementById('filter-date').value;
  let f = allRecords;
  if (df) f = f.filter(r => (r.device_id || '默认') === df);
  if (dt) f = f.filter(r => r.time.startsWith(dt));
  const tb = document.querySelector('#records tbody');
  tb.innerHTML = '';
  f.slice(-20).reverse().forEach(r => {
    const name = deviceNames[r.device_id] || r.device_id || '默认';
    tb.innerHTML += `<tr><td>${r.time}</td><td>${name}</td><td>${r.heart_rate || '-'}</td><td>${r.steps || '-'}</td></tr>`;
  });
}

async function load() {
  const data = await fetch('/api/records?limit=100').then(r => r.json());
  allRecords = data.records;

  const last = allRecords[allRecords.length - 1];
  renderGauge(last ? last.heart_rate : 0);

  const deviceSet = new Set(allRecords.map(r => r.device_id || '默认'));
  const sel = document.getElementById('filter-device');
  const cur = sel.value;
  sel.innerHTML = '<option value="">全部设备</option>' + Array.from(deviceSet).map(d => `<option value="${d}">${deviceNames[d] || d}</option>`).join('');
  sel.value = cur;
  renderRecords();

  const now = Date.now();
  const devices = {};
  allRecords.forEach(r => {
    const id = r.device_id || '默认';
    if (!devices[id]) devices[id] = {count: 0, last: 0};
    devices[id].count++;
    const t = new Date(r.time).getTime();
    if (t > devices[id].last) devices[id].last = t;
  });
  document.getElementById('devices').innerHTML = Object.keys(devices).map(id => {
    const online = (now - devices[id].last) < 30000;
    return `<span class="device-tag ${online ? 'online' : 'offline'}">${online ? '🟢' : '⚫'} ${deviceNames[id] || id}（${devices[id].count} 条）</span>`;
  }).join('');

  const labels = allRecords.slice(-20).map(r => r.time.slice(-8));
  const hr = allRecords.slice(-20).map(r => r.heart_rate || 0);
  const st = allRecords.slice(-20).map(r => r.steps || 0);
  const ctx = document.getElementById('chart').getContext('2d');
  if (window.myChart) window.myChart.destroy();
  window.myChart = new Chart(ctx, {
    type: 'line',
    data: { labels, datasets: [
      { label: '心率', data: hr, borderColor: '#ef4444', backgroundColor: 'rgba(239,68,68,0.1)', fill: true, tension: 0.4, borderWidth: 2, pointRadius: 0 },
      { label: '步数', data: st, borderColor: '#6366f1', backgroundColor: 'rgba(99,102,241,0.1)', fill: true, tension: 0.4, borderWidth: 2, pointRadius: 0 }
    ]},
    options: { responsive: true, maintainAspectRatio: true, animation: { duration: 600 } }
  });

  const stats = await fetch('/api/stats').then(r => r.json());
  document.getElementById('today-steps').innerText = stats.today_steps;
  document.getElementById('week-steps').innerText = stats.week_steps;
  document.getElementById('today-hr').innerText = stats.today_hr || '-';
  document.getElementById('week-hr').innerText = stats.week_hr || '-';
}

async function clearData() {
  if (!confirm('确定要清空所有记录吗？')) return;
  await fetch('/clear', { method: 'POST' });
  load();
}

socket.on('new_data', (data) => {
  showToast(`收到新数据：${data.device_id || '默认'}`);
  load();
});

load();
renderHeatmap();
renderCalendar();
renderCompare();
setInterval(load, 10000);
</script>
</body>
</html>
'''

if __name__ == '__main__':
    socketio.run(app, host='0.0.0.0', port=5000, debug=False,
                  allow_unsafe_werkzeug=True)
