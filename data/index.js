document.oncontextmenu = (event) => {
    if (event.target && event.target.tagName.toLowerCase() === 'input' && (event.target.type.toLowerCase() === 'text' || event.target.type.toLowerCase() === 'password'))
        return;
    else {
        event.preventDefault(); event.stopPropagation(); return false;
    }
};
Date.prototype.toJSON = function () {
    let tz = this.getTimezoneOffset();
    let sign = tz > 0 ? '-' : '+';
    let tzHrs = Math.floor(Math.abs(tz) / 60).fmt('00');
    let tzMin = (Math.abs(tz) % 60).fmt('00');
    return `${this.getFullYear()}-${(this.getMonth() + 1).fmt('00')}-${this.getDate().fmt('00')}T${this.getHours().fmt('00')}:${this.getMinutes().fmt('00')}:${this.getSeconds().fmt('00')}.${this.getMilliseconds().fmt('000')}${sign}${tzHrs}${tzMin}`;
};
Number.prototype.round = function (dec) { return Number(Math.round(this + 'e' + dec) + 'e-' + dec); };
Number.prototype.fmt = function (format, empty) {
    if (isNaN(this)) return empty || '';
    if (typeof format === 'undefined') return this.toString();
    let isNegative = this < 0;
    let tok = ['#', '0'];
    let pfx = '', sfx = '', fmt = format.replace(/[^#\.0\,]/g, '');
    let dec = fmt.lastIndexOf('.') > 0 ? fmt.length - (fmt.lastIndexOf('.') + 1) : 0,
        fw = '', fd = '', vw = '', vd = '', rw = '', rd = '';
    let val = String(Math.abs(this).round(dec));
    let ret = '', commaChar = ',', decChar = '.';
    for (var i = 0; i < format.length; i++) {
        let c = format.charAt(i);
        if (c === '#' || c === '0' || c === '.' || c === ',')
            break;
        pfx += c;
    }
    for (let i = format.length - 1; i >= 0; i--) {
        let c = format.charAt(i);
        if (c === '#' || c === '0' || c === '.' || c === ',')
            break;
        sfx = c + sfx;
    }
    if (dec > 0) {
        let dp = val.lastIndexOf('.');
        if (dp === -1) {
            val += '.'; dp = 0;
        }
        else
            dp = val.length - (dp + 1);
        while (dp < dec) {
            val += '0';
            dp++;
        }
        fw = fmt.substring(0, fmt.lastIndexOf('.'));
        fd = fmt.substring(fmt.lastIndexOf('.') + 1);
        vw = val.substring(0, val.lastIndexOf('.'));
        vd = val.substring(val.lastIndexOf('.') + 1);
        let ds = val.substring(val.lastIndexOf('.'), val.length);
        for (let i = 0; i < fd.length; i++) {
            if (fd.charAt(i) === '#' && vd.charAt(i) !== '0') {
                rd += vd.charAt(i);
                continue;
            } else if (fd.charAt(i) === '#' && vd.charAt(i) === '0') {
                var np = vd.substring(i);
                if (np.match('[1-9]')) {
                    rd += vd.charAt(i);
                    continue;
                }
                else
                    break;
            }
            else if (fd.charAt(i) === '0' || fd.charAt(i) === '#')
                rd += vd.charAt(i);
        }
        if (rd.length > 0) rd = decChar + rd;
    }
    else {
        fw = fmt;
        vw = val;
    }
    var cg = fw.lastIndexOf(',') >= 0 ? fw.length - fw.lastIndexOf(',') - 1 : 0;
    var nw = Math.abs(Math.floor(this.round(dec)));
    if (!(nw === 0 && fw.substr(fw.length - 1) === '#') || fw.substr(fw.length - 1) === '0') {
        var gc = 0;
        for (let i = vw.length - 1; i >= 0; i--) {
            rw = vw.charAt(i) + rw;
            gc++;
            if (gc === cg && i !== 0) {
                rw = commaChar + rw;
                gc = 0;
            }
        }
        if (fw.length > rw.length) {
            var pstart = fw.indexOf('0');
            if (pstart >= 0) {
                var plen = fw.length - pstart;
                var pos = fw.length - rw.length - 1;
                while (rw.length < plen) {
                    let pc = fw.charAt(pos);
                    if (pc === ',') pc = commaChar;
                    rw = pc + rw;
                    pos--;
                }
            }
        }
    }
    if (isNegative) rw = '-' + rw;
    if (rd.length === 0 && rw.length === 0) return '';
    return pfx + rw + rd + sfx;
};
function makeBool(val) {
    if (typeof (val) === 'boolean') return val;
    if (typeof (val) === 'undefined') return false;
    if (typeof (val) === 'number') return val >= 1;
    if (typeof (val) === 'string') {
        if (val === '') return false;
        switch (val.toLowerCase().trim()) {
            case 'on':
            case 'true':
            case 'yes':
            case 'y':
                return true;
            case 'off':
            case 'false':
            case 'no':
            case 'n':
                return false;
        }
        if (!isNaN(parseInt(val, 10))) return parseInt(val, 10) >= 1;
    }
    return false;
}
function waitMessage(el) {
    let div = document.createElement('div');
    div.innerHTML = '<div class="lds-roller"><div></div><div></div><div></div><div></div><div></div><div></div><div></div><div></div></div></div>';
    div.classList.add('waitoverlay');
    el.appendChild(div);
    return div;
}
function clearErrors() {
    let errors = document.querySelectorAll('div.errorMessage');
    if (errors && errors.length > 0) errors.forEach((el) => { el.remove(); });
}
function serviceError(el, err) {
    let msg = '';
    if (typeof err === 'string' && err.startsWith('{')) {
        let e = JSON.parse(err);
        if (typeof e !== 'undefined' && typeof e.desc === 'string') msg = e.desc;
        else msg = err;
    }
    else if (typeof err === 'string') {
        msg = err;
    }
    else if (typeof err === 'number') {
        switch (err) {
            case 404:
                msg = `404: Service not found`;
                break;
            default:
                msg = `${err}: Network HTTP Error`;
                break;
        }
    }
    else if (typeof err !== 'undefined') {
        console.log(err);
        if (typeof err.desc === 'string') msg = typeof err.desc !== 'undefined' ? err.desc : err.message;
    }
    return errorMessage(el, msg);
}
function errorMessage(el, msg) {
    let div = document.createElement('div');
    div.innerHTML = '<div class="innerError">' + msg + '</div><button type="button" onclick="clearErrors();">Close</button></div>';
    div.classList.add('errorMessage');
    el.appendChild(div);
    return div;
}
function socketError(el, msg) {
    let div = document.createElement('div');
    div.innerHTML = '<div id="divSocketAttempts" style="position:absolute;width:100%;left:0px;padding-right:24px;text-align:right;top:0px;font-size:18px;"><span>Attempts: </span><span id="spanSocketAttempts"></span></div><div class="innerError"><div>Could not connect to server</div><hr></hr><div style="font-size:.7em">' + msg + '</div></div>';
    div.classList.add('errorMessage');
    div.classList.add('socket-error');
    el.appendChild(div);
    return div;
}
function promptMessage(el, msg, onYes) {
    let div = document.createElement('div');
    div.innerHTML = '<div class="innerError">' + msg + '</div><button id="btnYes" type="button">Yes</button><button type="button" onclick="clearErrors();">No</button></div>';
    div.classList.add('errorMessage');
    el.appendChild(div);
    div.querySelector('#btnYes').addEventListener('click', onYes);
    return div;
}
function getJSON(url, cb) {
    let xhr = new XMLHttpRequest();
    console.log({ get: url });
    xhr.open('GET', url, true);
    xhr.responseType = 'json';
    xhr.onload = () => {
        let status = xhr.status;
        cb(status === 200 ? null : status, xhr.response);
    };
    xhr.onerror = (evt) => {
        cb(xhr.status || 500, xhr.statusText);
    };
    xhr.send();
}
function getText(url, cb) {
    let xhr = new XMLHttpRequest();
    console.log({ get: url });
    xhr.open('GET', url, true);
    xhr.responseType = 'text';
    xhr.onload = () => {
        let status = xhr.status;
        cb(status === 200 ? null : status, xhr.responseText);
    };
    xhr.onerror = (evt) => {
        cb(xhr.status || 500, xhr.statusText);
    };
    xhr.send();
}
function putJSON(url, data, cb) {
    let xhr = new XMLHttpRequest();
    console.log({ put: url, data: data });
    xhr.open('PUT', url, true);
    xhr.responseType = 'json';
    xhr.setRequestHeader('Content-Type', 'application/json; charset=utf-8');
    xhr.setRequestHeader('Accept', 'application/json');
    xhr.onload = () => {
        let status = xhr.status;
        cb(status === 200 ? null : status, xhr.response);
    };
    xhr.onerror = (evt) => {
        cb(xhr.status || 500, xhr.statusText);
    };
    xhr.send(JSON.stringify(data));
}
var socket;
var tConnect = null;
var sockIsOpen = false;
var connecting = false;
var connects = 0;
var connectFailed = 0;
async function initSockets() {
    if (connecting) return;
    console.log('Connecting to socket...');
    connecting = true;
    if (tConnect) clearTimeout(tConnect);
    tConnect = null;
    let wms = document.getElementsByClassName('socket-wait');
    for (let i = 0; i < wms.length; i++) {
        wms[i].remove();
    }
    waitMessage(document.getElementById('divContainer')).classList.add('socket-wait');
    try {
        socket = new WebSocket(`ws://${window.location.hostname}:8080/`);
        socket.onmessage = (evt) => {
            if (evt.data.startsWith('42')) {
                let ndx = evt.data.indexOf(',');
                let eventName = evt.data.substring(3, ndx);
                let data = evt.data.substring(ndx + 1, evt.data.length - 1);
                try {
                    var reISO = /^(\d{4}|\+010000)-(\d{2})-(\d{2})T(\d{2}):(\d{2}):(\d{2}(?:\.\d*))(?:Z|(\+|-)([\d|:]*))?$/;
                    var reMsAjax = /^\/Date\((d|-|.*)\)[\/|\\]$/;
                    var msg = JSON.parse(data, (key, value) => {
                        if (typeof value === 'string') {
                            var a = reISO.exec(value);
                            if (a) return new Date(value);
                            a = reMsAjax.exec(value);
                            if (a) {
                                var b = a[1].split(/[-+,.]/);
                                return new Date(b[0] ? +b[0] : 0 - +b[1]);
                            }
                        }
                        return value;
                    });
                    switch (eventName) {
                        case 'remoteFrame':
                            somfy.procRemoteFrame(msg);
                            break;
                        case 'shadeState':
                            somfy.procShadeState(msg);
                            break;
                        case 'shadeRemoved':
                            break;
                        case 'shadeAdded':
                            break;
                        case 'ethernet':
                            wifi.procEthernet(msg);
                            break;
                        case 'wifiStrength':
                            wifi.procWifiStrength(msg);
                            break;
                        case 'packetPulses':
                            console.log(msg);
                            break;

                    }
                } catch (err) {
                    console.log({ eventName: eventName, data: data, err: err });
                }
            }
        };
        socket.onopen = (evt) => {
            if (tConnect) clearTimeout(tConnect);
            tConnect = null;
            console.log({ msg: 'open', evt: evt });
            sockIsOpen = true;
            connecting = false;
            connects++;
            connectFailed = 0;
            let wms = document.getElementsByClassName('socket-wait');
            for (let i = 0; i < wms.length; i++) {
                wms[i].remove();
            }
            let errs = document.getElementsByClassName('socket-error');
            for (let i = 0; i < errs.length; i++)
                errs[i].remove();
            if (general.reloadApp) {
                general.reload();
            }
            else {
                (async () => {
                    await general.init();
                    await somfy.init();
                    await mqtt.init();
                    await wifi.init();
                })();
            }
        };
        socket.onclose = (evt) => {
            wifi.procWifiStrength({ ssid: '', channel: -1, strength: -100 });
            wifi.procEthernet({ connected: '', speed: 0, fullduplex: false });
            if (document.getElementsByClassName('socket-wait').length === 0)
                waitMessage(document.getElementById('divContainer')).classList.add('socket-wait');
            if (evt.wasClean) {
                console.log({ msg: 'close-clean', evt: evt });
                connectFailed = 0;
                tConnect = setTimeout(async () => { await reopenSocket(); }, 7000);
                console.log('Reconnecting socket in 7 seconds');
            }
            else {
                console.log({ msg: 'close-died', reason: evt.reason, evt: evt, sock: socket });
                if (connects > 0) {
                    console.log('Reconnecting socket in 3 seconds');
                    tConnect = setTimeout(async () => { await reopenSocket(); }, 3000);
                }
                else {
                    if (connecting) {
                        connectFailed++;
                        let timeout = Math.min(connectFailed * 500, 10000);
                        console.log(`Initial socket did not connect try again (server was busy and timed out ${connectFailed} times)`);
                        tConnect = setTimeout(async () => { await reopenSocket(); }, timeout);
                        if (connectFailed === 5) {
                            socketError(document.getElementById('divContainer'), 'Too many clients connected.  A maximum of 5 clients may be connected at any one time.  Close some connections to the ESP Somfy RTS device to proceed.');
                        }
                        let spanAttempts = document.getElementById('spanSocketAttempts');
                        if (spanAttempts) spanAttempts.innerHTML = connectFailed.fmt("#,##0");

                    }
                    else {
                        console.log('Connecting socket in .5 seconds');
                        tConnect = setTimeout(async () => { await reopenSocket(); }, 500);
                    }

                }

            }
            connecting = false;
        };
        socket.onerror = (evt) => {
            console.log({ msg: 'socket error', evt: evt, sock: socket });
        };
    } catch (err) {
        console.log({
            msg: 'Websocket connection error', err: err
        });
        tConnect = setTimeout(async () => { await reopenSocket(); }, 5000);
    }
}
async function reopenSocket() {
    if (tConnect) clearTimeout(tConnect);
    tConnect = null;
    await initSockets();
}
class General {
    appVersion = 'v1.7.1';
    reloadApp = false;
    async init() {
        this.setAppVersion();
        this.setTimeZones();
        this.loadGeneral();
        if (sockIsOpen && this.isConfigOpen()) socket.send('join:0');
    };
    reload() {
        let addMetaTag = (name, content) => {
            let meta = document.createElement('meta');
            meta.httpEquiv = name;
            meta.content = content;
            document.getElementsByTagName('head')[0].appendChild(meta);
        }
        addMetaTag('pragma', 'no-cache');
        addMetaTag('expires', '0');
        addMetaTag('cache-control', 'no-cache');
        document.location.reload();
    }
    timeZones = [
    { city: 'Africa/Cairo', code: 'EET-2' },
    { city: 'Africa/Johannesburg', code: 'SAST-2' },
    { city: 'Africa/Juba', code: 'CAT-2' },
    { city: 'Africa/Lagos', code: 'WAT-1' },
    { city: 'Africa/Mogadishu', code: 'EAT-3' },
    { city: 'Africa/Tunis', code: 'CET-1' },
    { city: 'America/Adak', code: 'HST10HDT,M3.2.0,M11.1.0' },
    { city: 'America/Anchorage', code: 'AKST9AKDT,M3.2.0,M11.1.0' },
    { city: 'America/Asuncion', code: '<-04>4<-03>,M10.1.0/0,M3.4.0/0' },
    { city: 'America/Bahia_Banderas', code: 'CST6CDT,M4.1.0,M10.5.0' },
    { city: 'America/Barbados', code: 'AST4' },
    { city: 'America/Bermuda', code: 'AST4ADT,M3.2.0,M11.1.0' },
    { city: 'America/Cancun', code: 'EST5' },
    { city: 'America/Central_Time', code: 'CST6CDT,M3.2.0,M11.1.0' },
    { city: 'America/Chihuahua', code: 'MST7MDT,M4.1.0,M10.5.0' },
    { city: 'America/Eastern_Time', code: 'EST5EDT,M3.2.0,M11.1.0' },
    { city: 'America/Godthab', code: '<-03>3<-02>,M3.5.0/-2,M10.5.0/-1' },
    { city: 'America/Havana', code: 'CST5CDT,M3.2.0/0,M11.1.0/1' },
    { city: 'America/Mexico_City', code: 'CST6' },
    { city: 'America/Miquelon', code: '<-03>3<-02>,M3.2.0,M11.1.0' },
    { city: 'America/Mountain_Time', code: 'MST7MDT,M3.2.0,M11.1.0' },
    { city: 'America/Pacific_Time', code: 'PST8PDT,M3.2.0,M11.1.0' },
    { city: 'America/Phoenix', code: 'MST7' },
    { city: 'America/Santiago', code: '<-04>4<-03>,M9.1.6/24,M4.1.6/24' },
    { city: 'America/St_Johns', code: 'NST3:30NDT,M3.2.0,M11.1.0' },
    { city: 'Antarctica/Troll', code: '<+00>0<+02>-2,M3.5.0/1,M10.5.0/3' },
    { city: 'Asia/Amman', code: 'EET-2EEST,M2.5.4/24,M10.5.5/1' },
    { city: 'Asia/Beirut', code: 'EET-2EEST,M3.5.0/0,M10.5.0/0' },
    { city: 'Asia/Colombo', code: '<+0530>-5:30' },
    { city: 'Asia/Damascus', code: 'EET-2EEST,M3.5.5/0,M10.5.5/0' },
    { city: 'Asia/Gaza', code: 'EET-2EEST,M3.4.4/50,M10.4.4/50' },
    { city: 'Asia/Hong_Kong', code: 'HKT-8' },
    { city: 'Asia/Jakarta', code: 'WIB-7' },
    { city: 'Asia/Jayapura', code: 'WIT-9' },
    { city: 'Asia/Jerusalem', code: 'IST-2IDT,M3.4.4/26,M10.5.0' },
    { city: 'Asia/Kabul', code: '<+0430>-4:30' },
    { city: 'Asia/Karachi', code: 'PKT-5' },
    { city: 'Asia/Kathmandu', code: '<+0545>-5:45' },
    { city: 'Asia/Kolkata', code: 'IST-5:30' },
    { city: 'Asia/Makassar', code: 'WITA-8' },
    { city: 'Asia/Manila', code: 'PST-8' },
    { city: 'Asia/Seoul', code: 'KST-9' },
    { city: 'Asia/Shanghai', code: 'CST-8' },
    { city: 'Asia/Tehran', code: '<+0330>-3:30' },
    { city: 'Asia/Tokyo', code: 'JST-9' },
    { city: 'Atlantic/Azores', code: '<-01>1<+00>,M3.5.0/0,M10.5.0/1' },
    { city: 'Australia/Adelaide', code: 'ACST-9:30ACDT,M10.1.0,M4.1.0/3' },
    { city: 'Australia/Brisbane', code: 'AEST-10' },
    { city: 'Australia/Darwin', code: 'ACST-9:30' },
    { city: 'Australia/Eucla', code: '<+0845>-8:45' },
    { city: 'Australia/Lord_Howe', code: '<+1030>-10:30<+11>-11,M10.1.0,M4.1.0' },
    { city: 'Australia/Melbourne', code: 'AEST-10AEDT,M10.1.0,M4.1.0/3' },
    { city: 'Australia/Perth', code: 'AWST-8' },
    { city: 'Etc/GMT-1', code: '<+01>-1' },
    { city: 'Etc/GMT-2', code: '<+02>-2' },
    { city: 'Etc/GMT-3', code: '<+03>-3' },
    { city: 'Etc/GMT-4', code: '<+04>-4' },
    { city: 'Etc/GMT-5', code: '<+05>-5' },
    { city: 'Etc/GMT-6', code: '<+06>-6' },
    { city: 'Etc/GMT-7', code: '<+07>-7' },
    { city: 'Etc/GMT-8', code: '<+08>-8' },
    { city: 'Etc/GMT-9', code: '<+09>-9' },
    { city: 'Etc/GMT-10',code: '<+10>-10' },
    { city: 'Etc/GMT-11', code: '<+11>-11' },
    { city: 'Etc/GMT-12', code: '<+12>-12' },
    { city: 'Etc/GMT-13', code: '<+13>-13' },
    { city: 'Etc/GMT-14', code: '<+14>-14' },
    { city: 'Etc/GMT+0', code: 'GMT0' },
    { city: 'Etc/GMT+1', code: '<-01>1' },
    { city: 'Etc/GMT+2', code: '<-02>2' },
    { city: 'Etc/GMT+3', code: '<-03>3' },
    { city: 'Etc/GMT+4', code: '<-04>4' },
    { city: 'Etc/GMT+5', code: '<-05>5' },
    { city: 'Etc/GMT+6', code: '<-06>6' },
    { city: 'Etc/GMT+7', code: '<-07>7' },
    { city: 'Etc/GMT+8', code: '<-08>8' },
    { city: 'Etc/GMT+9', code: '<-09>9' },
    { city: 'Etc/GMT+10', code: '<-10>10' },
    { city: 'Etc/GMT+11', code: '<-11>11' },
    { city: 'Etc/GMT+12', code: '<-12>12' },
    { city: 'Etc/UTC', code: 'UTC0' },
    { city: 'Europe/Athens', code: 'EET-2EEST,M3.5.0/3,M10.5.0/4' },
    { city: "Europe/Berlin", code: "CEST-1CET,M3.2.0/2:00:00,M11.1.0/2:00:00" },
    { city: 'Europe/Brussels', code: 'CET-1CEST,M3.5.0,M10.5.0/3' },
    { city: 'Europe/Chisinau', code: 'EET-2EEST,M3.5.0,M10.5.0/3' },
    { city: 'Europe/Dublin', code: 'IST-1GMT0,M10.5.0,M3.5.0/1' },
    { city: 'Europe/Lisbon',  code: 'WET0WEST,M3.5.0/1,M10.5.0' },
    { city: 'Europe/London', code: 'GMT0BST,M3.5.0/1,M10.5.0' },
    { city: 'Europe/Moscow', code: 'MSK-3' },
    { city: 'Indian/Cocos',  code: '<+0630>-6:30' },
    { city: 'Pacific/Auckland', code: 'NZST-12NZDT,M9.5.0,M4.1.0/3' },
    { city: 'Pacific/Chatham', code: '<+1245>-12:45<+1345>,M9.5.0/2:45,M4.1.0/3:45' },
    { city: 'Pacific/Easter', code: '<-06>6<-05>,M9.1.6/22,M4.1.6/22' },
    { city: 'Pacific/Fiji', code: '<+12>-12<+13>,M11.2.0,M1.2.3/99' },
    { city: 'Pacific/Guam',  code: 'ChST-10' },
    { city: 'Pacific/Honolulu', code: 'HST10' },
    { city: 'Pacific/Marquesas', code: '<-0930>9:30' },
    { city: 'Pacific/Midway',  code: 'SST11' },
    { city: 'Pacific/Norfolk', code: '<+11>-11<+12>,M10.1.0,M4.1.0/3' }
    ];
    loadGeneral() {
        let overlay = waitMessage(document.getElementById('fsGeneralSettings'));
        getJSON('/modulesettings', (err, settings) => {
            overlay.remove();
            if (err) {
                console.log(err);
            }
            else {
                console.log(settings);
                let dd = document.getElementById('selTimeZone');
                for (let i = 0; i < dd.options.length; i++) {
                    if (dd.options[i].value === settings.posixZone) {
                        dd.selectedIndex = i;
                        break;
                    }
                }
                general.setAppVersion();
                document.getElementById('spanFwVersion').innerText = settings.fwVersion;
                document.getElementsByName('hostname')[0].value = settings.hostname;
                document.getElementsByName('ntptimeserver')[0].value = settings.ntpServer;
                document.getElementsByName('ssdpBroadcast')[0].checked = settings.ssdpBroadcast;
                document.getElementById('fldSsid').value = settings.ssid;
                document.getElementById('fldPassphrase').value = settings.passphrase;
            }
        });

    };
    setAppVersion() { document.getElementById('spanAppVersion').innerText = this.appVersion; };
    setTimeZones() {
        let dd = document.getElementById('selTimeZone');
        dd.length = 0;
        let maxLength = 0;
        for (let i = 0; i < this.timeZones.length; i++) {
            let opt = document.createElement('option');
            opt.text = this.timeZones[i].city;
            opt.value = this.timeZones[i].code;
            maxLength = Math.max(maxLength, this.timeZones[i].code.length);
            dd.add(opt);
        }
        dd.value = 'UTC0';
        console.log(`Max TZ:${maxLength}`);
    };
    setGeneral() {
        let valid = true;
        let obj = {
            hostname: document.getElementsByName('hostname')[0].value,
            posixZone: document.getElementById('selTimeZone').value,
            ntpServer: document.getElementsByName('ntptimeserver')[0].value,
            ssdpBroadcast: document.getElementsByName('ssdpBroadcast')[0].checked
        }
        console.log(obj);
        if (typeof obj.hostname === 'undefined' || !obj.hostname || obj.hostname === '') {
            errorMessage(document.getElementById('fsGeneralSettings'), 'You must supply a valid host name.');
            valid = false;
        }
        if (valid && !/^[a-zA-Z0-9-]+$/.test(obj.hostname)) {
            errorMessage(document.getElementById('fsGeneralSettings'), 'The host name must only include numbers, letters, or dash.');
            valid = false;
        }
        if (valid && obj.hostname.length > 32) {
            errorMessage(document.getElementById('fsGeneralSettings'), 'The host name can only be up to 32 characters long.');
            valid = false;
        }
        if (valid) {
            if (document.getElementById('btnSaveGeneral').classList.contains('disabled')) return;
            document.getElementById('btnSaveGeneral').classList.add('disabled');
            let overlay = waitMessage(document.getElementById('fsGeneralSettings'));
            putJSON('/setgeneral', obj, (err, response) => {
                overlay.remove();
                document.getElementById('btnSaveGeneral').classList.remove('disabled');
                console.log(response);
            });
        }
    };
    rebootDevice() {
        promptMessage(document.getElementById('fsGeneralSettings'), 'Are you sure you want to reboot the device?', () => {
            socket.close(3000, 'reboot');
            let overlay = waitMessage(document.getElementById('fsGeneralSettings'));
            putJSON('/reboot', {}, (err, response) => {
                overlay.remove();
                document.getElementById('btnSaveGeneral').classList.remove('disabled');
                console.log(response);
            });
            clearErrors();
        });
    };
    isConfigOpen() {
        let divCfg = document.getElementById('divConfigPnl');
        return (window.getComputedStyle(divCfg).display !== 'none');
    };
    toggleConfig() {
        let divCfg = document.getElementById('divConfigPnl');
        let divHome = document.getElementById('divHomePnl');
        if (window.getComputedStyle(divCfg).display === 'none') {
            divHome.style.display = 'none';
            divCfg.style.display = '';
            document.getElementById('icoConfig').className = 'icss-home';
            if (sockIsOpen) socket.send('join:0');
        }
        else {
            divHome.style.display = '';
            divCfg.style.display = 'none';
            document.getElementById('icoConfig').className = 'icss-gear';
            if (sockIsOpen) socket.send('leave:0');
       }
        somfy.closeEditShade();
        somfy.closeConfigTransceiver();
    };
};
var general = new General();
class Wifi {
    ethBoardTypes = [{ val: 0, label: 'Custom Config' },
    { val: 1, label: 'WT32-ETH01', clk: 0, ct: 0, addr: 1, pwr: 16, mdc: 23, mdio: 18 },
    { val: 2, label: 'Olimex ESP32-POE', clk: 3, ct: 0, addr: 0, pwr: 12, mdc: 23, mdio: 18 },
    { val: 3, label: 'Olimex ESP32-EVB', clk: 0, ct: 0, addr: 0, pwr: -1, mdc: 23, mdio: 18 },
    { val: 4, label: 'LILYGO T-Internet POE', clk: 3, ct: 0, addr: 0, pwr: 16, mdc: 23, mdio: 18 },
    { val: 5, label: 'wESP32 v7+', clk: 0, ct: 2, addr: 0, pwr: -1, mdc: 16, mdio: 17 },
    { val: 6, label: 'wESP32 < v7', clk: 0, ct: 0, addr: 0, pwr: -1, mdc: 16, mdio: 17 }
    ];
    ethClockModes = [{ val: 0, label: 'GPIO0 IN' }, { val: 1, label: 'GPIO0 OUT' }, { val: 2, label: 'GPIO16 OUT' }, { val: 3, label: 'GPIO17 OUT' }];
    ethPhyTypes = [{ val: 0, label: 'LAN8720' }, { val: 1, label: 'TLK110' }, { val: 2, label: 'RTL8201' }, { val: 3, label: 'DP83848' }, { val: 4, label: 'DM9051' }, { val: 5, label: 'KZ8081' }];
    init() {
        document.getElementById("divNetworkStrength").innerHTML = this.displaySignal(-100);
        let addr = [];
        this.loadETHDropdown(document.getElementById('selETHClkMode'), this.ethClockModes);
        this.loadETHDropdown(document.getElementById('selETHPhyType'), this.ethPhyTypes);
        this.loadETHDropdown(document.getElementById('selETHBoardType'), this.ethBoardTypes);
        for (let i = 0; i < 32; i++) addr.push({ val: i, label: `PHY ${i}` });
        this.loadETHDropdown(document.getElementById('selETHAddress'), addr);
        this.loadETHPins(document.getElementById('selETHPWRPin'), 'power');
        this.loadETHPins(document.getElementById('selETHMDCPin'), 'mdc', 23);
        this.loadETHPins(document.getElementById('selETHMDIOPin'), 'mdio', 18);
        if (typeof document.querySelector('div.tab-container > span.selected[data-grpid="fsWiFiSettings"]') !== 'undefined') {
            this.loadNetwork();
        }
    };
    loadETHPins(sel, type, selected) {
        let arr = [];
        switch (type) {
            case 'power':
                arr.push({ val: -1, label: 'None' });
                break;
        }
        for (let i = 0; i < 36; i++) {
            arr.push({ val: i, label: `GPIO ${i}` });
        }
        this.loadETHDropdown(sel, arr, selected);
    };
    loadETHDropdown(sel, arr, selected) {
        while (sel.firstChild) sel.removeChild(sel.firstChild);
        for (let i = 0; i < arr.length; i++) {
            let elem = arr[i];
            sel.options[sel.options.length] = new Option(elem.label, elem.val, elem.val === selected, elem.val === selected);
        }
    };
    onETHBoardTypeChanged(sel) {
        let type = this.ethBoardTypes.find(elem => parseInt(sel.value, 10) === elem.val);
        if (typeof type !== 'undefined') {
            // Change the values to represent what the board type says.
            if(typeof type.ct !== 'undefined') document.getElementById('selETHPhyType').value = type.ct;
            if (typeof type.clk !== 'undefined') document.getElementById('selETHClkMode').value = type.clk;
            if (typeof type.addr !== 'undefined') document.getElementById('selETHAddress').value = type.addr;
            if (typeof type.pwr !== 'undefined') document.getElementById('selETHPWRPin').value = type.pwr;
            if (typeof type.mdc !== 'undefined') document.getElementById('selETHMDCPin').value = type.mdc;
            if (typeof type.mdio !== 'undefined') document.getElementById('selETHMDIOPin').value = type.mdio;
            document.getElementById('divETHSettings').style.display = type.val === 0 ? '' : 'none';
        }
    };
    onDHCPClicked(cb) { document.getElementById('divStaticIP').style.display = cb.checked ? 'none' : ''; };
    loadNetwork() {
        let overlay = waitMessage(document.getElementById('fsWiFiSettings'));
        getJSON('/networksettings', (err, settings) => {
            overlay.remove();
            console.log(settings);
            if (err) {
                serviceError(document.getElementById('fsWiFiSettings'), err);
            }
            else {
                document.getElementById('fldSsid').value = settings.wifi.ssid;
                document.getElementById('fldPassphrase').value = settings.wifi.passphrase;
                document.getElementById('selETHBoardType').value = settings.ethernet.boardType;
                document.getElementById('cbUseDHCP').checked = settings.ethernet.dhcp;
                document.getElementById('cbHardwired').checked = settings.connType >= 2;
                document.getElementById('cbFallbackWireless').checked = settings.connType === 3;
                document.getElementById('selETHPhyType').value = settings.ethernet.phyType;
                document.getElementById('selETHAddress').value = settings.ethernet.phyAddress;
                document.getElementById('selETHClkMode').value = settings.ethernet.CLKMode;
                document.getElementById('selETHPWRPin').value = settings.ethernet.PWRPin;
                document.getElementById('selETHMDCPin').value = settings.ethernet.MDCPin;
                document.getElementById('selETHMDIOPin').value = settings.ethernet.MDIOPin;
                document.getElementById('fldIPAddress').value = settings.ethernet.ip;
                document.getElementById('fldSubnetMask').value = settings.ethernet.subnet;
                document.getElementById('fldGateway').value = settings.ethernet.gateway;
                document.getElementById('fldDNS1').value = settings.ethernet.dns1;
                document.getElementById('fldDNS2').value = settings.ethernet.dns2;
                if (settings.connType >= 2) {
                    document.getElementById('divWiFiMode').style.display = 'none';
                    document.getElementById('divEthernetMode').style.display = '';
                    document.getElementById('divFallbackWireless').style.display = 'inline-block';
                }
                else {
                    document.getElementById('divWiFiMode').style.display = '';
                    document.getElementById('divEthernetMode').style.display = 'none';
                    document.getElementById('divFallbackWireless').style.display = 'none';
                }
                if (settings.ethernet.boardType === 0) {
                    document.getElementById('divETHSettings').style.display = '';
                }
                else {
                    document.getElementById('divETHSettings').style.display = 'none';
                }
            }
        });

    };
    useEthernetClicked() {
        let useEthernet = document.getElementById('cbHardwired').checked;
        document.getElementById('divWiFiMode').style.display = useEthernet ? 'none' : '';
        document.getElementById('divEthernetMode').style.display = useEthernet ? '' : 'none';
        document.getElementById('divFallbackWireless').style.display = useEthernet ? 'inline-block' : 'none';
    }
    async loadAPs() {
        if (document.getElementById('btnScanAPs').classList.contains('disabled')) return;
        document.getElementById('divAps').innerHTML = '<div style="display:flex;justify-content:center;align-items:center;"><div class="lds-roller"><div></div><div></div><div></div><div></div><div></div><div></div><div></div><div></div></div></div>';
        document.getElementById('btnScanAPs').classList.add('disabled');
        //document.getElementById('btnConnectWiFi').classList.add('disabled');
        getJSON('/scanaps', (err, aps) => {
            document.getElementById('btnScanAPs').classList.remove('disabled');
            //document.getElementById('btnConnectWiFi').classList.remove('disabled');
            console.log(aps);
            if (err) {
                this.displayAPs({ connected: { name: '', passphrase: '' }, accessPoints: [] });
            }
            else {
                this.displayAPs(aps);
            }
        });
    };
    displayAPs(aps) {
        let div = '';
        let nets = [];
        for (let i = 0; i < aps.accessPoints.length; i++) {
            let ap = aps.accessPoints[i];
            let p = nets.find(elem => elem.name === ap.name);
            if (typeof p !== 'undefined' && p) {
                p.channel = p.strength > ap.strength ? p.channel : ap.channel;
                p.macAddress = p.strength > ap.strength ? p.macAddress : ap.macAddress;
                p.strength = Math.max(p.strength, ap.strength);
            }
            else
                nets.push(ap);
        }
        // Sort by the best signal strength.
        nets.sort((a, b) => b.strength - a.strength);
        for (let i = 0; i < nets.length; i++) {
            let ap = nets[i];
            div += `<div class="wifiSignal" onclick="wifi.selectSSID(this);" data-channel="${ap.channel}" data-encryption="${ap.encryption}" data-strength="${ap.strength}" data-mac="${ap.macAddress}"><span class="ssid">${ap.name}</span><span class="strength">${this.displaySignal(ap.strength)}</span></div>`;
        }
        let divAps = document.getElementById('divAps');
        divAps.setAttribute('data-lastloaded', new Date().getTime());
        divAps.innerHTML = div;
        //document.getElementsByName('ssid')[0].value = aps.connected.name;
        //document.getElementsByName('passphrase')[0].value = aps.connected.passphrase;
        //this.procWifiStrength(aps.connected);
    };
    selectSSID(el) {
        let obj = {
            name: el.querySelector('span.ssid').innerHTML,
            encryption: el.getAttribute('data-encryption'),
            strength: parseInt(el.getAttribute('data-strength'), 10),
            channel: parseInt(el.getAttribute('data-channel'), 10)
        }
        console.log(obj);
        document.getElementsByName('ssid')[0].value = obj.name;
    };
    calcWaveStrength(sig) {
        let wave = 0;
        if (sig > -90) wave++;
        if (sig > -80) wave++;
        if (sig > -70) wave++;
        if (sig > -67) wave++;
        if (sig > -30) wave++;
        return wave;
    };
    displaySignal(sig) {
        return `<div class="signal waveStrength-${this.calcWaveStrength(sig)}"><div class="wv4 wave"><div class="wv3 wave"><div class="wv2 wave"><div class="wv1 wave"><div class="wv0 wave"></div></div></div></div></div></div>`;
    };
    saveNetwork() {
        
        let obj = {
            connType: document.getElementById('cbHardwired').checked ?  document.getElementById('cbFallbackWireless').checked ? 3 : 2 : 1,
            wifi: {},
            ethernet: {}
        };
        if (obj.connType >= 2) {
            // We are connecting to a LAN but we need the user to be sure about this since
            // the information needs to be correct.  Incorrect settings can destroy the board.
            obj.ethernet = {
                boardType: parseInt(document.getElementById('selETHBoardType').value, 10),
                phyType: parseInt(document.getElementById('selETHPhyType').value, 10),
                phyAddress: parseInt(document.getElementById('selETHAddress').value, 10),
                dhcp: document.getElementById('cbUseDHCP').checked,
                CLKMode: parseInt(document.getElementById('selETHClkMode').value, 10),
                PWRPin: parseInt(document.getElementById('selETHPWRPin').value, 10),
                MDCPin: parseInt(document.getElementById('selETHMDCPin').value, 10),
                MDIOPin: parseInt(document.getElementById('selETHMDIOPin').value, 10),
                ip: document.getElementById('fldIPAddress').value,
                subnet: document.getElementById('fldSubnetMask').value,
                gateway: document.getElementById('fldGateway').value,
                dns1: document.getElementById('fldDNS1').value,
                dns2: document.getElementById('fldDNS2').value
            }


            let boardType = this.ethBoardTypes.find(elem => obj.ethernet.boardType === elem.val);
            let phyType = this.ethPhyTypes.find(elem => obj.ethernet.phyType === elem.val);
            let clkMode = this.ethClockModes.find(elem => obj.ethernet.CLKMode === elem.val);
            let div = document.createElement('div');
            let html = `<form id="frmSetLAN"><div id="divLanSettings" class="instructions" style="display:block;height:auto;min-height:100%">`;
            html += '<div style="width:100%;color:red;text-align:center;font-weight:bold;"><span style="background:yellow;padding:10px;display:inline-block;border-radius:5px;background:white;">BEWARE ... WARNING ... DANGER<span></div>';
            html += '<hr style="width:100%;margin:0px;"></hr>';
            html += '<p style="font-size:14px;">Incorrect Ethernet settings can damage your ESP32.  Please verify the settings below and ensure they match the manufacturer spec sheet.</p>';
            html += '<p style="font-size:14px;">If you are unsure do not press the Red button and press the Green button.  If any of the settings are incorrect please use the Custom Board type and set them to the correct values.';
            html += '<hr/><div>';
            html += `<div class="eth-setting-line"><label>Board Type</label><span>${boardType.label} [${boardType.val}]</span></div>`;
            html += `<div class="eth-setting-line"><label>PHY Chip Type</label><span>${phyType.label} [${phyType.val}]</span></div>`;
            html += `<div class="eth-setting-line"><label>PHY Address</label><span>${obj.ethernet.phyAddress}</span ></div >`;
            html += `<div class="eth-setting-line"><label>Clock Mode</label><span>${clkMode.label} [${clkMode.val}]</span></div >`;
            html += `<div class="eth-setting-line"><label>Power Pin</label><span>${obj.ethernet.PWRPin === -1 ? 'None' : obj.ethernet.PWRPin}</span></div>`;
            html += `<div class="eth-setting-line"><label>MDC Pin</label><span>${obj.ethernet.MDCPin}</span></div>`;
            html += `<div class="eth-setting-line"><label>MDIO Pin</label><span>${obj.ethernet.MDIOPin}</span></div>`;
            html += '</div>'
            html += `<div class="button-container">`
            html += `<button id="btnSaveEthernet" type="button" style="padding-left:20px;padding-right:20px;display:inline-block;background:orangered;">Save Ethernet Settings</button>`
            html += `<button id="btnCancel" type="button" style="padding-left:20px;padding-right:20px;display:inline-block;background:lawngreen;color:gray" onclick="document.getElementById('frmSetLAN').remove();">Cancel</button>`
            html += `</div><form>`;
            div.innerHTML = html;
            document.getElementById('divContainer').appendChild(div);
            div.querySelector('#btnSaveEthernet').addEventListener('click', (el, event) => {
                console.log(obj);
                document.getElementById('frmSetLAN').remove();
                this.sendNetworkSettings(obj);
            });
        }
        else {
            obj.wifi = {
                ssid: document.getElementsByName('ssid')[0].value,
                passphrase: document.getElementsByName('passphrase')[0].value
            };
            this.sendNetworkSettings(obj);
        }
    }
    sendNetworkSettings(obj) {
        if (document.getElementById('btnSaveNetwork').classList.contains('disabled')) return;
        document.getElementById('btnSaveNetwork').classList.add('disabled');
        let overlay = waitMessage(document.getElementById('divContainer'));
        putJSON('/setNetwork', obj, (err, response) => {
            overlay.remove();
            document.getElementById('btnSaveNetwork').classList.remove('disabled');
            console.log(response);
        });
    }
    connectWiFi() {
        if (document.getElementById('btnConnectWiFi').classList.contains('disabled')) return;
        document.getElementById('btnConnectWiFi').classList.add('disabled');
        let obj = {
            ssid: document.getElementsByName('ssid')[0].value,
            passphrase: document.getElementsByName('passphrase')[0].value
        }
        let overlay = waitMessage(document.getElementById('fsWiFiSettings'));
        putJSON('/connectwifi', obj, (err, response) => {
            overlay.remove();
            document.getElementById('btnConnectWiFi').classList.remove('disabled');
            console.log(response);

        });
    };
    procWifiStrength(strength) {
        let ssid = strength.ssid || strength.name;
        document.getElementById('spanNetworkSSID').innerHTML = !ssid || ssid === '' ? '-------------' : ssid;
        document.getElementById('spanNetworkChannel').innerHTML = isNaN(strength.channel) || strength.channel < 0 ? '--' : strength.channel;
        let cssClass = 'waveStrength-' + ((isNaN(strength.strength) || strength > 0) ? -100 : this.calcWaveStrength(strength.strength));
        let elWave = document.getElementById('divNetworkStrength').children[0];
        if (typeof elWave !== 'undefined' && !elWave.classList.contains(cssClass)) {
            elWave.classList.remove('waveStrength-0', 'waveStrength-1', 'waveStrength-2', 'waveStrength-3', 'waveStrength-4');
            elWave.classList.add(cssClass);
        }
        document.getElementById('spanNetworkStrength').innerHTML = (isNaN(strength.strength) || strength.strength <= -100) ? '----' : strength.strength;
    }
    procEthernet(ethernet) {
        console.log(ethernet);
        document.getElementById('divEthernetStatus').style.display = ethernet.connected ? '' : 'none';
        document.getElementById('divWiFiStrength').style.display = ethernet.connected ? 'none' : '';
        document.getElementById('spanEthernetStatus').innerHTML = ethernet.connected ? 'Connected' : 'Disconnected';
        document.getElementById('spanEthernetSpeed').innerHTML = !ethernet.connected ? '--------' : `${ethernet.speed}Mbps ${ethernet.fullduplex ? 'Full-duplex' : 'Half-duplex'}`;
    }
};
var wifi = new Wifi();
class Somfy {
    frames = [];
    async init() {
        this.loadPins('inout', document.getElementById('selTransSCKPin'));
        this.loadPins('inout', document.getElementById('selTransCSNPin'));
        this.loadPins('inout', document.getElementById('selTransMOSIPin'));
        this.loadPins('input', document.getElementById('selTransMISOPin'));
        this.loadPins('out', document.getElementById('selTransTXPin'));
        this.loadPins('input', document.getElementById('selTransRXPin'));
        this.loadSomfy();
    }
    async loadSomfy() {
        console.log(self);
        let overlay = waitMessage(document.getElementById('fsSomfySettings'));
        getJSON('/controller', (err, somfy) => {
            overlay.remove();
            if (err) {
                console.log(err);
                serviceError(document.getElementById('fsSomfySettings'), err);
            }
            else {
                console.log(somfy);
                document.getElementById('selTransSCKPin').value = somfy.transceiver.config.SCKPin.toString();
                document.getElementById('selTransCSNPin').value = somfy.transceiver.config.CSNPin.toString();
                document.getElementById('selTransMOSIPin').value = somfy.transceiver.config.MOSIPin.toString();
                document.getElementById('selTransMISOPin').value = somfy.transceiver.config.MISOPin.toString();
                document.getElementById('selTransTXPin').value = somfy.transceiver.config.TXPin.toString();
                document.getElementById('selTransRXPin').value = somfy.transceiver.config.RXPin.toString();
                document.getElementById('selRadioType').value = somfy.transceiver.config.type;
                document.getElementById('selRadioProto').value = somfy.transceiver.config.proto;
                document.getElementById('spanMaxShades').innerText = somfy.maxShades;
                document.getElementById('spanFrequency').innerText = (Math.round(somfy.transceiver.config.frequency * 1000) / 1000).fmt('#,##0.000');
                document.getElementById('slidFrequency').value = Math.round(somfy.transceiver.config.frequency * 1000);
                document.getElementById('spanRxBandwidth').innerText = (Math.round(somfy.transceiver.config.rxBandwidth * 100) / 100).fmt('#,##0.00');
                document.getElementById('slidRxBandwidth').value = Math.round(somfy.transceiver.config.rxBandwidth * 100);
                document.getElementById('spanTxPower').innerText = somfy.transceiver.config.txPower;
                document.getElementById('spanDeviation').innerText = (Math.round(somfy.transceiver.config.deviation * 100) / 100).fmt('#,##0.00');
                document.getElementById('slidDeviation').value = Math.round(somfy.transceiver.config.deviation * 100);
                document.getElementsByName('enableRadio')[0].checked = somfy.transceiver.config.enabled;
                if (somfy.transceiver.config.radioInit) {
                    document.getElementById('divRadioError').style.display = 'none';
                }
                else {
                    document.getElementById('divRadioError').style.display = '';
                }


                let tx = document.getElementById('slidTxPower');
                let lvls = [-30, -20, -15, -10, -6, 0, 5, 7, 10, 11, 12];
                for (let i = lvls.length - 1; i >= 0; i--) {
                    if (lvls[i] === somfy.transceiver.config.txPower) {
                        tx.value = i;
                    }
                    else if (lvls[i < somfy.transceiver.config.txPower] < somfy.transceiver.txPower) {
                        tx.value = i + 1;
                    }
                }

                // Create the shades list.
                this.setShadesList(somfy.shades);
            }
        });
    };
    saveRadio() {
        let valid = true;
        let getIntValue = (fld) => { return parseInt(document.getElementById(fld).value, 10); }
        let obj = {
            enabled: document.getElementsByName('enableRadio')[0].checked,
            type: parseInt(document.getElementById('selRadioType').value, 10),
            proto: parseInt(document.getElementById('selRadioProto').value, 10),
            SCKPin: getIntValue('selTransSCKPin'),
            CSNPin: getIntValue('selTransCSNPin'),
            MOSIPin: getIntValue('selTransMOSIPin'),
            MISOPin: getIntValue('selTransMISOPin'),
            TXPin: getIntValue('selTransTXPin'),
            RXPin: getIntValue('selTransRXPin'),
            frequency: (Math.round(parseFloat(document.getElementById('spanFrequency').innerText) * 1000)) / 1000,
            rxBandwidth: (Math.round(parseFloat(document.getElementById('spanRxBandwidth').innerText) * 100)) / 100,
            txPower: parseInt(document.getElementById('spanTxPower').innerText, 10),
            deviation: (Math.round(parseFloat(document.getElementById('spanDeviation').innerText) * 100)) / 100
        };
        console.log(obj);
        // Check to make sure we have a trans type.
        if (typeof obj.type === 'undefined' || obj.type === '' || obj.type === 'none') {
            errorMessage(document.getElementById('fsSomfySettings'), 'You must select a radio type.');
            valid = false;
        }
        // Check to make sure no pins were duplicated and defined
        if (valid) {
            let fnValDup = (o, name) => {
                let val = o[name];
                if (typeof val === 'undefined' || isNaN(val)) {
                    errorMessage(document.getElementById('fsSomfySettings'), 'You must define all the pins for the radio.');
                    return false;
                }
                for (let s in o) {
                    if (s.endsWith('Pin') && s !== name) {
                        let sval = o[s];
                        if (typeof sval === 'undefined' || isNaN(sval)) {
                            errorMessage(document.getElementById('fsSomfySettings'), 'You must define all the pins for the radio.');
                            return false;
                        }
                        if (sval === val) {
                            if ((name === 'TXPin' && s === 'RXPin') ||
                                (name === 'RXPin' && s === 'TXPin'))
                                continue; // The RX and TX pins can share the same value.  In this instance the radio will only use GDO0.
                            else {
                                errorMessage(document.getElementById('fsSomfySettings'), `The ${name.replace('Pin', '')} pin is duplicated by the ${s.replace('Pin', '')}.  All pin definitions must be unique`);
                                valid = false;
                                return false;
                            }
                        }
                    }
                }
                return true;
            };
            if (valid) valid = fnValDup(obj, 'SCKPin');
            if (valid) valid = fnValDup(obj, 'CSNPin');
            if (valid) valid = fnValDup(obj, 'MOSIPin');
            if (valid) valid = fnValDup(obj, 'MISOPin');
            if (valid) valid = fnValDup(obj, 'TXPin');
            if (valid) valid = fnValDup(obj, 'RXPin');
            if (valid) {
                let overlay = waitMessage(document.getElementById('fsSomfySettings'));
                putJSON('/saveRadio', { config: obj }, (err, trans) => {
                    overlay.remove();
                    if (err) {
                        serviceError(document.getElementById('fsSomfySettings'), err);
                    }
                    else {
                        document.getElementById('btnSaveRadio').classList.remove('disabled');
                        if (trans.config.radioInit) {
                            document.getElementById('divRadioError').style.display = 'none';
                        }
                        else {
                            document.getElementById('divRadioError').style.display = '';
                        }
                    }
                    console.log(trans);
                });
            }
        }
    }
    btnDown = null;
    btnTimer = null;
    setShadesList(shades) {
        let divCfg = '';
        let divCtl = '';
        for (let i = 0; i < shades.length; i++) {
            let shade = shades[i];
            divCfg += `<div class="somfyShade" data-shadeid="${shade.shadeId}" data-remoteaddress="${shade.remoteAddress}" data-tilt="${shade.tiltType}" data-shadetype="${shade.shadeType}">`;
            divCfg += `<div class="button-outline" onclick="somfy.openEditShade(${shade.shadeId});"><i class="icss-edit"></i></div>`;
            //divCfg += `<i class="shade-icon" data-position="${shade.position || 0}%"></i>`;
            divCfg += `<span class="shade-name">${shade.name}</span>`;
            divCfg += `<span class="shade-address">${shade.remoteAddress}</span>`;
            divCfg += `<div class="button-outline" onclick="somfy.deleteShade(${shade.shadeId});"><i class="icss-trash"></i></div>`;
            divCfg += '</div>';

            divCtl += `<div class="somfyShadeCtl" data-shadeId="${shade.shadeId}" data-direction="${shade.direction}" data-remoteaddress="${shade.remoteAddress}" data-position="${shade.position}" data-target="${shade.target}" data-mypos="${shade.myPos}" data-mytiltpos="${shade.myTiltPos} data-shadetype="${shade.shadeType}" data-tilt="${shade.tiltType}"`;
            divCtl += ` data-windy="${(shade.flags & 0x10) === 0x10 ? 'true' : false}" data-sunny=${(shade.flags & 0x20) === 0x20 ? 'true' : 'false'}`;
            if (shade.tiltType !== 0) {
                divCtl += ` data-tiltposition="${shade.tiltPosition}" data-tiltdirection="${shade.tiltDirection}" data-tilttarget="${shade.tiltTarget}"`;
            }
            divCtl += `><div class="shade-icon" data-shadeid="${shade.shadeId}" onclick="event.stopPropagation(); console.log(event); somfy.openSetPosition(${shade.shadeId});">`;
            divCtl += `<i class="somfy-shade-icon`;
            switch (shade.shadeType) {
                case 1:
                    divCtl += ' icss-window-blind';
                    break;
                case 3:
                    divCtl += ' icss-awning';
                    break;
                default:
                    divCtl += ' icss-window-shade';
                    break;
            }
            divCtl += `" data-shadeid="${shade.shadeId}" style="--shade-position:${shade.flipPosition ? 100 - shade.position : shade.position}%;vertical-align: top;"></i>`;
            divCtl += shade.tiltType !== 0 ? `<i class="icss-window-tilt" data-shadeid="${shade.shadeId}" data-tiltposition="${shade.tiltPosition}"></i></div>` : '</div>';
            divCtl += `<div class="indicator indicator-wind"><i class="icss-warning"></i></div><div class="indicator indicator-sun"><i class="icss-sun"></i></div>`;
            divCtl += `<div class="shade-name">`;

            divCtl += `<span class="shadectl-name">${shade.name}</span>`;
            divCtl += `<span class="shadectl-mypos"><label>My: </label><span id="spanMyPos">${shade.myPos > 0 ? shade.myPos + '%' : '---'}</span>`
            if (shade.myTiltPos > 0) divCtl += `<label> Tilt: </label><span id="spanMyTiltPos">${shade.myTiltPos > 0 ? shade.myTiltPos + '%' : '---'}</span>`
            divCtl += '</div>';

            divCtl += `<div class="shadectl-buttons">`;
            divCtl += `<div class="button-sunflag cmd-button" data-cmd="sunflag" data-shadeid="${shade.shadeId}" data-on="${shade.flags & 0x01 ? 'true' : 'false'}" style="${shade.shadeType !== 3 ? 'display:none' : ''}"><i class="icss-sun-c"></i><i class="icss-sun-o"></i></div>`;
            divCtl += `<div class="button-outline cmd-button" data-cmd="up" data-shadeid="${shade.shadeId}"><i class="icss-somfy-up"></i></div>`;
            divCtl += `<div class="button-outline cmd-button my-button" data-cmd="my" data-shadeid="${shade.shadeId}" style="font-size:2em;padding:10px;"><span>my</span></div>`;
            divCtl += `<div class="button-outline cmd-button" data-cmd="down" data-shadeid="${shade.shadeId}"><i class="icss-somfy-down" style="margin-top:-4px;"></i></div>`;
            divCtl += '</div></div>';
        }
        document.getElementById('divShadeList').innerHTML = divCfg;
        let shadeControls = document.getElementById('divShadeControls');
        shadeControls.innerHTML = divCtl;
        // Attach the timer for setting the My Position for the shade.
        let btns = shadeControls.querySelectorAll('div.cmd-button');
        for (let i = 0; i < btns.length; i++) {
            btns[i].addEventListener('mouseup', (event) => {
                console.log(this);
                console.log(event);
                console.log('mouseup');
                let cmd = event.currentTarget.getAttribute('data-cmd');
                let shadeId = parseInt(event.currentTarget.getAttribute('data-shadeid'), 10);
                if (this.btnTimer) {
                    clearTimeout(this.btnTimer);
                    this.btnTimer = null;
                    if (new Date().getTime() - this.btnDown > 2000) event.preventDefault();
                    else this.sendCommand(shadeId, cmd);
                }
                else if (cmd === 'sunflag') {
                    if (makeBool(event.currentTarget.getAttribute('data-on')))
                        this.sendCommand(shadeId, 'flag');
                    else
                        this.sendCommand(shadeId, 'sunflag');
                }
                else this.sendCommand(shadeId, cmd);
            }, true);
            btns[i].addEventListener('mousedown', (event) => {
                if (this.btnTimer) {
                    clearTimeout(this.btnTimer);
                    this.btnTimer = null;
                }
                console.log(this);
                console.log(event);
                console.log('mousedown');
                let elShade = event.currentTarget.closest('div.somfyShadeCtl');
                let cmd = event.currentTarget.getAttribute('data-cmd');
                let shadeId = parseInt(event.currentTarget.getAttribute('data-shadeid'), 10);
                let el = event.currentTarget.closest('.somfyShadeCtl');
                this.btnDown = new Date().getTime();
                if (cmd === 'my') {
                    if (parseInt(el.getAttribute('data-direction'), 10) === 0) {
                        this.btnTimer = setTimeout(() => {
                            // Open up the set My Position dialog.  We will allow the user to change the position to match
                            // the desired position.
                            this.openSetMyPosition(shadeId);
                        }, 2000);
                    }
                }
                else if (makeBool(elShade.getAttribute('data-tilt'))) {
                    this.btnTimer = setTimeout(() => {
                        this.sendTiltCommand(shadeId, cmd);
                    }, 2000);
                }
            }, true);
            btns[i].addEventListener('touchstart', (event) => {
                if (this.btnTimer) {
                    clearTimeout(this.btnTimer);
                    this.btnTimer = null;
                }
                console.log(this);
                console.log(event);
                console.log('touchstart');
                let elShade = event.currentTarget.closest('div.somfyShadeCtl');
                let cmd = event.currentTarget.getAttribute('data-cmd');
                let shadeId = parseInt(event.currentTarget.getAttribute('data-shadeid'), 10);
                let el = event.currentTarget.closest('.somfyShadeCtl');
                this.btnDown = new Date().getTime();
                if (parseInt(el.getAttribute('data-direction'), 10) === 0) {
                    if (cmd === 'my') {
                        this.btnTimer = setTimeout(() => {
                            // Open up the set My Position dialog.  We will allow the user to change the position to match
                            // the desired position.
                            this.openSetMyPosition(shadeId);
                        }, 2000);
                    }
                    else {
                        if (makeBool(elShade.getAttribute('data-tilt'))) {
                            this.btnTimer = setTimeout(() => {
                                this.sendTiltCommand(shadeId, cmd);
                            }, 2000);
                        }
                    }
                }
            }, true);
        }
    };
    closeShadePositioners() {
        let ctls = document.querySelectorAll('.shade-positioner');
        for (let i = 0; i < ctls.length; i++) {
            console.log('Closing shade positioner');
            ctls[i].remove();
        }
    }
    openSetMyPosition(shadeId) {
        if (typeof shadeId === 'undefined') return;
        else {
            let shade = document.querySelector(`div.somfyShadeCtl[data-shadeid="${shadeId}"]`);
            if (shade) {
                this.closeShadePositioners();
                let currPos = parseInt(shade.getAttribute('data-position'), 10);
                let currTiltPos = parseInt(shade.getAttribute('data-tiltposition'), 10);
                let myPos = parseInt(shade.getAttribute('data-mypos'), 10);
                let tiltType = parseInt(shade.getAttribute('data-tilt'), 10);
                let myTiltPos = parseInt(shade.getAttribute('data-mytiltpos'), 10);
                let elname = shade.querySelector(`.shadectl-name`);
                let shadeName = elname.innerHTML;
                let html = `<div class="shade-name"><span>${shadeName}</span><div style="float:right;vertical-align:top;cursor:pointer;font-size:12px;margin-top:4px;">`;
                if (myPos >= 0)
                    html += `<div onclick="document.getElementById('slidShadeTarget').value = ${myPos}; document.getElementById('slidShadeTarget').dispatchEvent(new Event('change'));"><span style="display:inline-block;width:47px;">Current:</span><span>${myPos}</span><span>%</span></div>`;
                if (myTiltPos >= 0 && tiltType > 0)
                    html += `<div onclick="document.getElementById('slidShadeTiltTarget').value = ${myTiltPos}; document.getElementById('slidShadeTarget').dispatchEvent(new Event('change'));"><span style="display:inline-block;width:47px;">Tilt:</span><span>${myTiltPos}</span><span>%</span></div>`;
                html += `</div></div >`;
                html += `<input id="slidShadeTarget" name="shadeTarget" type="range" min="0" max="100" step="1" oninput="document.getElementById('spanShadeTarget').innerHTML = this.value;" />`;
                html += `<label for="slidShadeTarget"><span>Target Position </span><span><span id="spanShadeTarget" class="shade-target">${currPos}</span><span>%</span></span></label>`;
                html += '<div id="divTiltTarget" style="display:none;">';
                html += `<input id="slidShadeTiltTarget" name="shadeTiltTarget" type="range" min="0" max="100" step="1" oninput="document.getElementById('spanShadeTiltTarget').innerHTML = this.value;" />`;
                html += `<label for="slidShadeTiltTarget"><span>Target Tilt </span><span><span id="spanShadeTiltTarget" class="shade-target">${currTiltPos}</span><span>%</span></span></label>`;
                html += '</div>'
                html += `<hr></hr>`;
                html += '<div style="text-align:right;width:100%;">'
                html += `<button id="btnSetMyPosition" type="button" style="width:auto;display:inline-block;padding-left:10px;padding-right:10px;margin-top:0px;margin-bottom:10px;margin-right:7px;">Set My Position</button>`;
                html += `<button id="btnCancel" type="button" onclick="somfy.closeShadePositioners();" style="width:auto;display:inline-block;padding-left:10px;padding-right:10px;margin-top:0px;margin-bottom:10px;">Cancel</button>`;
                html += `</div></div>`;
                let div = document.createElement('div');
                div.setAttribute('class', 'shade-positioner shade-my-positioner');
                div.setAttribute('data-shadeid', shadeId);
                div.style.height = 'auto';
                div.innerHTML = html;
                shade.appendChild(div);
                let elTarget = div.querySelector('input#slidShadeTarget');
                let elTiltTarget = div.querySelector('input#slidShadeTiltTarget');
                elTarget.value = currPos;
                elTiltTarget.value = currTiltPos;
                let elBtn = div.querySelector('button#btnSetMyPosition');
                if (tiltType > 0) div.querySelector('div#divTiltTarget').style.display = '';
                let fnProcessChange = () => {
                    let pos = parseInt(elTarget.value, 10);
                    let tilt = parseInt(elTiltTarget.value, 10);
                    if (pos === myPos && (tiltType === 0 || tilt === myTiltPos)) {
                        elBtn.innerHTML = 'Clear My Position';
                        elBtn.style.background = 'orangered';
                    }
                    else {
                        elBtn.innerHTML = 'Set My Position';
                        elBtn.style.background = '';
                    }
                    document.getElementById('spanShadeTiltTarget').innerHTML = tilt;
                    document.getElementById('spanShadeTarget').innerHTML = pos;
                };
                let fnSetMyPosition = () => {
                    let pos = parseInt(elTarget.value, 10);
                    let tilt = parseInt(elTiltTarget.value, 10);
                    somfy.sendShadeMyPosition(shadeId, pos, tilt);
                };
                fnProcessChange();
                elTarget.addEventListener('change', (event) => { fnProcessChange(); });
                elTiltTarget.addEventListener('change', (event) => { fnProcessChange(); });
                elBtn.addEventListener('click', (event) => { fnSetMyPosition(); });

            }
        }
    }
    sendShadeMyPosition(shadeId, pos, tilt) {
        console.log(`Sending My Position for shade id ${shadeId} to ${pos} and ${tilt}`);
        let overlay = waitMessage(document.getElementById('divContainer'));
        putJSON('/setMyPosition', { shadeId: shadeId, pos: pos, tilt: tilt }, (err, response) => {
            this.closeShadePositioners();
            overlay.remove();
            console.log(response);
        });
    }
    setLinkedRemotesList(shade) {
        let divCfg = '';
        for (let i = 0; i < shade.linkedRemotes.length; i++) {
            let remote = shade.linkedRemotes[i];
            divCfg += `<div class="somfyLinkedRemote" data-shadeid="${shade.shadeId}" data-remoteaddress="${remote.remoteAddress}" style="text-align:center;">`;
            divCfg += `<span class="linkedremote-address" style="display:inline-block;width:127px;text-align:left;">${remote.remoteAddress}</span>`;
            divCfg += `<span class="linkedremote-code" style="display:inline-block;width:77px;text-align:left;">${remote.lastRollingCode}</span>`;
            divCfg += `<div class="button-outline" onclick="somfy.unlinkRemote(${shade.shadeId}, ${remote.remoteAddress});"><i class="icss-trash"></i></div>`;
            divCfg += '</div>';
        }
        document.getElementById('divLinkedRemoteList').innerHTML = divCfg;
    };
    loadPins(type, sel, opt) {
        while (sel.firstChild) sel.removeChild(sel.firstChild);
        for (let i = 0; i < 40; i++) {
            switch (i) {
                case 6: // SPI Flash Pins
                case 7:
                case 8:
                case 9:
                case 10:
                case 11:
                    if(type !== 'inout' && type !== 'input') continue;
                    break;
                case 37:
                case 38:
                    continue;
                case 32: // We cannot use this pin with the mask for TX.
                case 33:
                case 34: // Input only
                case 35:
                case 36:
                case 39:
                    if (type !== 'input') continue;
                    break;
            }
            sel.options[sel.options.length] = new Option(`GPIO-${i > 9 ? i.toString() : '0' + i.toString()}`, i, typeof opt !== 'undefined' && opt === i);
        }
    };
    procShadeState(state) {
        console.log(state);
        let icons = document.querySelectorAll(`.somfy-shade-icon[data-shadeid="${state.shadeId}"]`);
        for (let i = 0; i < icons.length; i++) {
            icons[i].style.setProperty('--shade-position', `${state.flipPosition ? 100 - state.position : state.position}%`);
        }
        if (state.tiltType !== 0) {
            let tilts = document.querySelectorAll(`.icss-window-tilt[data-shadeid="${state.shadeId}"]`);
            for (let i = 0; i < tilts.length; i++) {
                tilts[i].setAttribute('data-tiltposition', `${state.tiltPosition}`);
            }
        }
        let flags = document.querySelectorAll(`.button-sunflag[data-shadeid="${state.shadeId}"]`);
        for (let i = 0; i < flags.length; i++) {
            flags[i].style.display = state.type === 3 ? '' : 'none';
            flags[i].setAttribute('data-on', state.flags & 0x01 === 0x01 ? 'true' : 'false');
           
        }
        let divs = document.querySelectorAll(`.somfyShadeCtl[data-shadeid="${state.shadeId}"]`);
        for (let i = 0; i < divs.length; i++) {
            divs[i].setAttribute('data-direction', state.direction);
            divs[i].setAttribute('data-position', state.position);
            divs[i].setAttribute('data-target', state.target);
            divs[i].setAttribute('data-mypos', state.myPos);
            divs[i].setAttribute('data-windy', (state.flags & 0x10) === 0x10 ? 'true' : 'false');
            divs[i].setAttribute('data-sunny', (state.flags & 0x20) === 0x20 ? 'true' : 'false');
            if (typeof state.myTiltPos !== 'undefined') divs[i].setAttribute('data-mytiltpos', state.myTiltPos);
            else divs[i].setAttribute('data-mytiltpos', -1);
            if (state.tiltType !== 0) {
                divs[i].setAttribute('data-tiltdirection', state.tiltDirection);
                divs[i].setAttribute('data-tiltposition', state.tiltPosition);
                divs[i].setAttribute('data-tilttarget', state.tiltTarget);
            }
            let span = divs[i].querySelector('#spanMyPos');
            if (span) span.innerHTML = typeof state.mypos !== 'undefined' && state.mypos >= 0 ? `${state.mypos}%` : '---';
            span = divs[i].querySelector('#spanMyTiltPos');
            if (span) span.innerHTML = typeof state.myTiltPos !== 'undefined' && state.myTiltPos >= 0 ? `${state.myTiltPos}%` : '---';
        }
    };
    procRemoteFrame(frame) {
        console.log(frame);
        document.getElementById('spanRssi').innerHTML = frame.rssi;
        document.getElementById('spanFrameCount').innerHTML = parseInt(document.getElementById('spanFrameCount').innerHTML, 10) + 1
        let lnk = document.getElementById('divLinking');
        if (lnk) {
            let obj = {
                shadeId: parseInt(lnk.dataset.shadeid, 10),
                remoteAddress: frame.address,
                rollingCode: frame.rcode
            };
            let overlay = waitMessage(document.getElementById('divLinking'));
            putJSON('/linkRemote', obj, (err, shade) => {
                console.log(shade);
                overlay.remove();
                lnk.remove();
                this.setLinkedRemotesList(shade);
            });
        }
        let frames = document.getElementById('divFrames');
        let row = document.createElement('div');
        row.classList.add('frame-row');
        row.setAttribute('data-valid', frame.valid);
        // The socket is not sending the current date so we will snag the current receive date from
        // the browser.
        let fnFmtDate = (dt) => {
            return `${(dt.getMonth() + 1).fmt('00')}/${dt.getDate().fmt('00')} ${dt.getHours().fmt('00')}:${dt.getMinutes().fmt('00')}:${dt.getSeconds().fmt('00')}.${dt.getMilliseconds().fmt('000')}`;
        }
        let fnFmtTime = (dt) => {
            return `${dt.getHours().fmt('00')}:${dt.getMinutes().fmt('00')}:${dt.getSeconds().fmt('00')}.${dt.getMilliseconds().fmt('000')}`;
        }
        frame.time = new Date();
        let html = `<span>${frame.encKey}</span><span>${frame.address}</span><span>${frame.command}</span><span>${frame.rcode}</span><span>${frame.rssi}dBm</span><span>${frame.bits}</span><span>${fnFmtTime(frame.time)}</span><div class="frame-pulses">`;
        for (let i = 0; i < frame.pulses.length; i++) {
            if (i !== 0) html += ',';
            html += `${frame.pulses[i]}`;
        }
        html += '</div>';
        row.innerHTML = html;
        frames.prepend(row);
        this.frames.push(frame);
    };
    JSONPretty(obj, indent = 2) {
        if (Array.isArray(obj)) {
            let output = '[';
            for (let i = 0; i < obj.length; i++) {
                if (i !== 0) output += ',\n';
                output += this.JSONPretty(obj[i], indent);
            }
            output += ']'
            return output;
        }
        else {
            let output = JSON.stringify(obj, function (k, v) {
                if (Array.isArray(v)) return JSON.stringify(v);
                return v;
            }, indent).replace(/\\/g, '')
                .replace(/\"\[/g, '[')
                .replace(/\]\"/g, ']')
                .replace(/\"\{/g, '{')
                .replace(/\}\"/g, '}')
                .replace(/\{\n\s+/g, '{');
            return output;
        }
    }
    framesToClipboard() {
        if (typeof navigator.clipboard !== 'undefined')
            navigator.clipboard.writeText(this.JSONPretty(this.frames, 2));
        else {
            let dummy = document.createElement('textarea');
            document.body.appendChild(dummy);
            dummy.value = this.JSONPretty(this.frames, 2);
            dummy.focus();
            dummy.select();
            document.execCommand('copy');
            document.body.removeChild(dummy);
        }
    }
    openConfigTransceiver() {
        document.getElementById('somfyMain').style.display = 'none';
        document.getElementById('somfyTransceiver').style.display = '';
    };
    closeConfigTransceiver() {
        document.getElementById('somfyTransceiver').style.display = 'none';
        document.getElementById('somfyMain').style.display = '';
    };
    onShadeTypeChanged(el) {
        let sel = document.getElementById('selShadeType');
        let tilt = parseInt(document.getElementById('selTiltType').value, 10);
        let ico = document.getElementById('icoShade');
        switch (parseInt(sel.value, 10)) {
            case 1:
                document.getElementById('divTiltSettings').style.display = '';
                if (ico.classList.contains('icss-window-shade')) ico.classList.remove('icss-window-shade');
                if (ico.classList.contains('icss-awning')) ico.classList.remove('icss-awning');
                if (!ico.classList.contains('icss-window-blind')) ico.classList.add('icss-window-blind');
                break;
            case 3:
                document.getElementById('divTiltSettings').style.display = 'none';
                if (ico.classList.contains('icss-window-shade')) ico.classList.remove('icss-window-shade');
                if (ico.classList.contains('icss-window-blind')) ico.classList.remove('icss-window-blind');
                if (!ico.classList.contains('icss-awning')) ico.classList.add('icss-awning');
                break;

            default:
                if (ico.classList.contains('icss-window-blind')) ico.classList.remove('icss-window-blind');
                if (ico.classList.contains('icss-awning')) ico.classList.remove('icss-awning');
                if (!ico.classList.contains('icss-window-shade')) ico.classList.add('icss-window-shade');
                document.getElementById('divTiltSettings').style.display = 'none';
                tilt = false;
                break;
        }
        document.getElementById('fldTiltTime').parentElement.style.display = tilt ? 'inline-block' : 'none';
        document.querySelector('#divSomfyButtons i.icss-window-tilt').style.display = tilt ? '' : 'none';
    };
    onShadeBitLengthChanged(el) {
        document.getElementById('divStepSettings').style.display = parseInt(el.value, 10) === 80 ? '' : 'none';
    }
    openEditShade(shadeId) {
        console.log('Opening Edit Shade');
        if (typeof shadeId === 'undefined') {
            let overlay = waitMessage(document.getElementById('fsSomfySettings'));
            getJSON('/getNextShade', (err, shade) => {
                overlay.remove();
                if (err) {
                    serviceError(document.getElementById('fsSomfySettings'), err);
                }
                else {
                    console.log(shade);
                    document.getElementById('btnPairShade').style.display = 'none';
                    document.getElementById('btnUnpairShade').style.display = 'none';
                    document.getElementById('btnLinkRemote').style.display = 'none';
                    document.getElementsByName('shadeUpTime')[0].value = 10000;
                    document.getElementsByName('shadeDownTime')[0].value = 10000;
                    document.getElementById('fldTiltTime').value = 7000;
                    document.getElementById('somfyMain').style.display = 'none';
                    document.getElementById('somfyShade').style.display = '';
                    document.getElementById('btnSaveShade').innerText = 'Add Shade';
                    document.getElementById('spanShadeId').innerText = '*';
                    document.getElementsByName('shadeName')[0].value = '';
                    document.getElementsByName('shadeAddress')[0].value = shade.remoteAddress;
                    document.getElementById('divLinkedRemoteList').innerHTML = '';
                    document.getElementById('btnSetRollingCode').style.display = 'none';
                    document.getElementById('selShadeBitLength').value = shade.bitLength || 56;
                    document.getElementById('selShadeProto').value = shade.proto || 0;
                    document.getElementById('slidStepSize').value = shade.stepSize || 100;
                    document.getElementById('spanStepSize').innerHTML = shade.stepSize.fmt('#,##0');
                    document.getElementById('cbFlipCommands').value = shade.flipCommands || false;
                    document.getElementById('cbFlipPosition').value = shade.flipPosition || false;
                }
            });
        }
        else {
            // Load up an exist shade.
            document.getElementById('btnSaveShade').style.display = 'none';
            document.getElementById('btnPairShade').style.display = 'none';
            document.getElementById('btnUnpairShade').style.display = 'none';
            document.getElementById('btnLinkRemote').style.display = 'none';

            document.getElementById('btnSaveShade').innerText = 'Save Shade';
            document.getElementById('spanShadeId').innerText = shadeId;
            let overlay = waitMessage(document.getElementById('fsSomfySettings'));
            getJSON(`/shade?shadeId=${shadeId}`, (err, shade) => {
                if (err) {
                    serviceError(document.getElementById('fsSomfySettings'), err);
                }
                else {
                    document.getElementById('somfyMain').style.display = 'none';
                    document.getElementById('somfyShade').style.display = '';
                    document.getElementById('btnSaveShade').style.display = 'inline-block';
                    document.getElementById('btnLinkRemote').style.display = '';
                    document.getElementById('selShadeType').value = shade.shadeType;
                    document.getElementById('selShadeBitLength').value = shade.bitLength;
                    document.getElementById('selShadeProto').value = shade.proto;
                    document.getElementsByName('shadeAddress')[0].value = shade.remoteAddress;
                    document.getElementsByName('shadeName')[0].value = shade.name;
                    document.getElementsByName('shadeUpTime')[0].value = shade.upTime;
                    document.getElementsByName('shadeDownTime')[0].value = shade.downTime;
                    document.getElementById('slidStepSize').value = shade.stepSize;
                    document.getElementById('spanStepSize').innerHTML = shade.stepSize.fmt('#,##0');
                    document.getElementById('fldTiltTime').value = shade.tiltTime;
                    document.getElementById('selTiltType').value = shade.tiltType;
                    document.getElementById('cbFlipCommands').checked = shade.flipCommands;
                    document.getElementById('cbFlipPosition').checked = shade.flipPosition;
                    this.onShadeTypeChanged(document.getElementById('selShadeType'));
                    let ico = document.getElementById('icoShade');
                    switch (shade.shadeType) {
                        case 1:
                            ico.classList.remove('icss-window-shade');
                            ico.classList.add('icss-window-blind');
                            break;
                        case 3:
                            ico.classList.remove('icss-window-shade');
                            ico.classList.add('icss-awning');
                            break;
                    }
                    let tilt = ico.parentElement.querySelector('i.icss-window-tilt');
                    tilt.style.display = shade.tiltType !== 0 ? '' : 'none';
                    tilt.setAttribute('data-tiltposition', shade.tiltPosition);
                    tilt.setAttribute('data-shadeid', shade.shadeId);
                    ico.style.setProperty('--shade-position', `${shade.flipPosition ? 100 - shade.position : shade.position}%`);
                    ico.style.setProperty('--tilt-position', `${shade.flipPosition ? 100 - shade.tiltPosition : shade.tiltPosition}%`);
                    ico.setAttribute('data-shadeid', shade.shadeId);
                    somfy.onShadeBitLengthChanged(document.getElementById('selShadeBitLength'));
                    document.getElementById('btnSetRollingCode').style.display = 'inline-block';
                    if (shade.paired) {
                        document.getElementById('btnUnpairShade').style.display = 'inline-block';
                    }
                    else {
                        document.getElementById('btnPairShade').style.display = 'inline-block';
                    }
                    this.setLinkedRemotesList(shade);
                }
                overlay.remove();
            });
        }
    };
    closeEditShade() {
        let el = document.getElementById('somfyShade');
        if (el) el.style.display = 'none';
        document.getElementById('somfyMain').style.display = '';
        //document.getElementById('somfyShade').style.display = 'none';
        el = document.getElementById('divLinking');
        if (el) el.remove();
        el = document.getElementById('divPairing');
        if (el) el.remove();
        el = document.getElementById('frmSetRollingCode');
        if (el) el.remove();

    };
    saveShade() {
        let shadeId = parseInt(document.getElementById('spanShadeId').innerText, 10);
        let obj = {
            remoteAddress: parseInt(document.getElementsByName('shadeAddress')[0].value, 10),
            name: document.getElementsByName('shadeName')[0].value,
            upTime: parseInt(document.getElementsByName('shadeUpTime')[0].value, 10),
            downTime: parseInt(document.getElementsByName('shadeDownTime')[0].value, 10),
            shadeType: parseInt(document.getElementById('selShadeType').value, 10),
            tiltTime: parseInt(document.getElementById('fldTiltTime').value, 10),
            bitLength: parseInt(document.getElementById('selShadeBitLength').value, 10) || 56,
            proto: parseInt(document.getElementById('selShadeProto').value, 10) || 0,
            stepSize: parseInt(document.getElementById('slidStepSize').value, 10) || 100,
            flipCommands: document.getElementById('cbFlipCommands').checked,
            flipPosition: document.getElementById('cbFlipPosition').checked
        };
        if (obj.shadeType === 1) {
            obj.tiltType = parseInt(document.getElementById('selTiltType').value, 10);
        }
        else obj.tiltType = 0;
        let valid = true;
        if (valid && (isNaN(obj.remoteAddress) || obj.remoteAddress < 1 || obj.remoteAddress > 16777215)) {
            errorMessage(document.getElementById('fsSomfySettings'), 'The remote address must be a number between 1 and 16777215.  This number must be unique for all shades.');
            valid = false;
        }
        if (valid && (typeof obj.name !== 'string' || obj.name === '' || obj.name.length > 20)) {
            errorMessage(document.getElementById('fsSomfySettings'), 'You must provide a name for the shade between 1 and 20 characters.');
            valid = false;
        }
        if (valid && (isNaN(obj.upTime) || obj.upTime < 1 || obj.upTime > 4294967295)) {
            errorMessage(document.getElementById('fsSomfySettings'), 'Up Time must be a value between 0 and 4,294,967,295 milliseconds.  This is the travel time to go from full closed to full open.');
            valid = false;
        }
        if (valid && (isNaN(obj.downTime) || obj.downTime < 1 || obj.downTime > 4294967295)) {
            errorMessage(document.getElementById('fsSomfySettings'), 'Down Time must be a value between 0 and 4,294,967,295 milliseconds.  This is the travel time to go from full open to full closed.');
            valid = false;
        }
        if (valid) {
            let overlay = waitMessage(document.getElementById('fsSomfySettings'));
            if (isNaN(shadeId) || shadeId >= 255) {
                // We are adding.
                putJSON('/addShade', obj, (err, shade) => {
                    console.log(shade);
                    document.getElementById('spanShadeId').innerText = shade.shadeId;
                    document.getElementById('btnSaveShade').innerText = 'Save Shade';

                    overlay.remove();
                    document.getElementById('btnSaveShade').style.display = 'inline-block';
                    document.getElementById('btnLinkRemote').style.display = '';
                    if (shade.paired) {
                        document.getElementById('btnUnpairShade').style.display = 'inline-block';
                    }
                    else {
                        document.getElementById('btnPairShade').style.display = 'inline-block';
                    }
                    document.getElementById('btnSetRollingCode').style.display = 'inline-block';

                });

            }
            else {
                obj.shadeId = shadeId;
                console.log(obj);
                putJSON('/saveShade', obj, (err, shade) => {
                    console.log(shade);
                    // We are updating.
                    overlay.remove();
                });
            }
            this.updateShadeList();
        }
    };
    updateShadeList() {
        let overlayCfg = waitMessage(document.getElementById('divShadeSection'));
        let overlayControl = waitMessage(document.getElementById('divShadeControls'));
        getJSON('/controller', (err, somfy) => {
            overlayCfg.remove();
            overlayControl.remove();
            if (err) {
                console.log(err);
                serviceError(document.getElementById('fsSomfySettings'), err);
            }
            else {
                console.log(somfy);
                // Create the shades list.
                this.setShadesList(somfy.shades);
            }
        });
    };
    deleteShade(shadeId) {
        let valid = true;
        if (isNaN(shadeId) || shadeId >= 255 || shadeId <= 0) {
            errorMessage(document.getElementById('fsSomfySettings'), 'A valid shade id was not supplied.');
            valid = false;
        }
        if (valid) {
            promptMessage(document.getElementById('fsSomfySettings'), 'Are you sure you want to delete this shade?', () => {
                clearErrors();
                let overlay = waitMessage(document.getElementById('fsSomfySettings'));
                putJSON('/deleteShade', { shadeId: shadeId }, (err, shade) => {
                    overlay.remove();
                    this.updateShadeList();
                });
            });
        }
    };
    sendPairCommand(shadeId) {
        putJSON('/pairShade', { shadeId: shadeId }, (err, shade) => {
            if (err) {
                console.log(err);
            }
            else {
                console.log(shade);
                document.getElementById('somfyMain').style.display = 'none';
                document.getElementById('somfyShade').style.display = '';
                document.getElementById('btnSaveShade').style.display = 'inline-block';
                document.getElementById('btnLinkRemote').style.display = '';
                document.getElementsByName('shadeAddress')[0].value = shade.remoteAddress;
                document.getElementsByName('shadeName')[0].value = shade.name;
                document.getElementsByName('shadeUpTime')[0].value = shade.upTime;
                document.getElementsByName('shadeDownTime')[0].value = shade.downTime;
                let ico = document.getElementById('icoShade');
                ico.style.setProperty('--shade-position', `${shade.flipPosition ? 100 - shade.position : shade.position}%`);
                ico.setAttribute('data-shadeid', shade.shadeId);
                if (shade.paired) {
                    document.getElementById('btnUnpairShade').style.display = 'inline-block';
                    document.getElementById('btnPairShade').style.display = 'none';
                }
                else {
                    document.getElementById('btnPairShade').style.display = 'inline-block';
                    document.getElementById('btnUnpairShade').style.display = 'none';
                }
                this.setLinkedRemotesList(shade);
                document.getElementById('divPairing').remove();
            }

        });
    };
    sendUnpairCommand(shadeId) {
        putJSON('/unpairShade', { shadeId: shadeId }, (err, shade) => {
            if (err) {
                console.log(err);
            }
            else {
                console.log(shade);
                document.getElementById('somfyMain').style.display = 'none';
                document.getElementById('somfyShade').style.display = '';
                document.getElementById('btnSaveShade').style.display = 'inline-block';
                document.getElementById('btnLinkRemote').style.display = '';
                document.getElementsByName('shadeAddress')[0].value = shade.remoteAddress;
                document.getElementsByName('shadeName')[0].value = shade.name;
                document.getElementsByName('shadeUpTime')[0].value = shade.upTime;
                document.getElementsByName('shadeDownTime')[0].value = shade.downTime;
                let ico = document.getElementById('icoShade');
                ico.style.setProperty('--shade-position', `${shade.flipPosition ? 100 - shade.position : shade.position}%`);
                ico.setAttribute('data-shadeid', shade.shadeId);
                if (shade.paired) {
                    document.getElementById('btnUnpairShade').style.display = 'inline-block';
                    document.getElementById('btnPairShade').style.display = 'none';
                }
                else {
                    document.getElementById('btnPairShade').style.display = 'inline-block';
                    document.getElementById('btnUnpairShade').style.display = 'none';
                }
                this.setLinkedRemotesList(shade);
                document.getElementById('divPairing').remove();
            }
        });
    };
    setRollingCode(shadeId, rollingCode) {
        let dlg = document.getElementById('frmSetRollingCode');
        let overlay = waitMessage(dlg || document.getElementById('fsSomfySettings'));
        putJSON('/setRollingCode', { shadeId: shadeId, rollingCode: rollingCode }, (err, shade) => {
            overlay.remove();
            if (err) {
                serviceError(document.getElementById('fsSomfySettings'), err);
            }
            else {
                if (dlg) dlg.remove();

            }
        });
    }
    openSetRollingCode(shadeId) {
        let overlay = waitMessage(document.getElementById('fsSomfySettings'));
        getJSON(`/shade?shadeId=${shadeId}`, (err, shade) => {
            overlay.remove();
            if (err) {
                serviceError(document.getElementById('fsSomfySettings'), err);
            }
            else {
                console.log(shade);
                let div = document.createElement('div');
                let html = `<form id="frmSetRollingCode"><div id="divRollingCode" class="instructions" data-shadeid="${shadeId}">`;
                html += '<div style="width:100%;color:red;text-align:center;font-weight:bold;"><span style="background:yellow;padding:10px;display:inline-block;border-radius:5px;background:white;">BEWARE ... WARNING ... DANGER<span></div>';
                html += '<hr style="width:100%;margin:0px;"></hr>';
                html += '<p style="font-size:14px;">If this shade is already paired with a motor then changing the rolling code WILL cause it to stop working.  Rolling codes are tied to the remote address and the Somfy motor expects these to be sequential.</p>';
                html += '<p style="font-size:14px;">If you hesitated just a little bit do not press the red button.  Green represents safety so press it, wipe the sweat from your brow, and go through the normal pairing process.'
                html += '<div class="field-group" style="border-radius:5px;background:white;width:50%;margin-left:25%;text-align:center">';
                html += `<input id="fldNewRollingCode" min="0" max="65535" name="newRollingCode" type="number" length="12" style="text-align:center;font-size:24px;" placeholder="New Code" value="${shade.lastRollingCode}"></input>`;
                html += '<label for="fldNewRollingCode">Rolling Code</label>';
                html += '</div>'
                html += `<div class="button-container">`
                html += `<button id="btnChangeRollingCode" type="button" style="padding-left:20px;padding-right:20px;display:inline-block;background:orangered;" onclick="somfy.setRollingCode(${shadeId}, parseInt(document.getElementById('fldNewRollingCode').value, 10));">Set Rolling Code</button>`
                html += `<button id="btnCancel" type="button" style="padding-left:20px;padding-right:20px;display:inline-block;background:lawngreen;color:gray" onclick="document.getElementById('frmSetRollingCode').remove();">Cancel</button>`
                html += `</div><form>`;
                div.innerHTML = html;
                document.getElementById('somfyShade').appendChild(div);
            }
        });
    }
    setPaired(shadeId, paired) {
        let obj = { shadeId: shadeId, paired: paired || false };
        let div = document.getElementById('divPairing');
        let overlay = typeof div === 'undefined' ? undefined : waitMessage(div);
        putJSON('/setPaired', obj, (err, shade) => {
            if (overlay) overlay.remove();
            if (err) {
                console.log(err);
                errorMessage(div, err.message);
            }
            else if (div) {
                console.log(shade);
                document.getElementById('somfyMain').style.display = 'none';
                document.getElementById('somfyShade').style.display = '';
                document.getElementById('btnSaveShade').style.display = 'inline-block';
                document.getElementById('btnLinkRemote').style.display = '';
                document.getElementsByName('shadeAddress')[0].value = shade.remoteAddress;
                document.getElementsByName('shadeName')[0].value = shade.name;
                document.getElementsByName('shadeUpTime')[0].value = shade.upTime;
                document.getElementsByName('shadeDownTime')[0].value = shade.downTime;
                let ico = document.getElementById('icoShade');
                ico.style.setProperty('--shade-position', `${shade.flipPosition ? 100 - shade.position : shade.position}%`);
                ico.setAttribute('data-shadeid', shade.shadeId);
                if (shade.paired) {
                    document.getElementById('btnUnpairShade').style.display = 'inline-block';
                    document.getElementById('btnPairShade').style.display = 'none';
                }
                else {
                    document.getElementById('btnPairShade').style.display = 'inline-block';
                    document.getElementById('btnUnpairShade').style.display = 'none';
                }
                this.setLinkedRemotesList(shade);
                div.remove();
            }
        });
    }
    pairShade(shadeId) {
        let div = document.createElement('div');
        let html = `<div id="divPairing" class="instructions" data-type="link-remote" data-shadeid="${shadeId}">`;
        html += '<div>Follow the instructions below to pair this shade with a Somfy motor</div>'
        html += '<hr style="width:100%;margin:0px;"></hr>';
        html += '<ul style="width:100%;margin:0px;padding-left:20px;font-size:14px;">';
        html += '<li>Open the shade memory using an existing remote</li>';
        html += '<li>Press the prog button on the back of the remote until the shade jogs</li>';
        html += '<li>After the shade jogs press the Prog button below</li>';
        html += '<li>The shade should jog again indicating that the shade is paired</li>';
        html += '<li>If the shade jogs, you can press the shade paired button.</li>'
        html += '<li>If the shade does not jog, press the prog button again.</li>'
        html += '</ul>'
        html += `<div class="button-container">`
        html += `<button id="btnSendPairing" type="button" style="padding-left:20px;padding-right:20px;display:inline-block;" onclick="somfy.sendCommand(${shadeId}, 'prog', 1);">Prog</button>`
        html += `<button id="btnMarkPaired" type="button" style="padding-left:20px;padding-right:20px;display:inline-block;" onclick="somfy.setPaired(${shadeId}, true);">Shade Paired</button>`
        html += `<button id="btnStopPairing" type="button" style="padding-left:20px;padding-right:20px;display:inline-block" onclick="document.getElementById('divPairing').remove();">Close</button>`
        html += `</div>`;
        div.innerHTML = html;
        document.getElementById('somfyShade').appendChild(div);
        return div;
    };
    unpairShade(shadeId) {
        let div = document.createElement('div');
        let html = `<div id="divPairing" class="instructions" data-type="link-remote" data-shadeid="${shadeId}">`;
        html += '<div>Follow the instructions below to unpair this shade from a Somfy motor</div>'
        html += '<hr style="width:100%;margin:0px;"></hr>';
        html += '<ul style="width:100%;margin:0px;padding-left:20px;font-size:14px;">';
        html += '<li>Open the shade memory using an existing remote</li>';
        html += '<li>Press the prog button on the back of the remote until the shade jogs</li>';
        html += '<li>After the shade jogs press the Prog button below</li>';
        html += '<li>The shade should jog again indicating that the shade is unpaired</li>';
        html += '<li>If the shade jogs, you can press the shade unpaired button.</li>'
        html += '<li>If the shade does not jog, press the prog button again until the shade jogs.</li>'
        html += '</ul>'
        html += `<div class="button-container">`
        html += `<button id="btnSendUnpairing" type="button" style="padding-left:20px;padding-right:20px;display:inline-block;" onclick="somfy.sendCommand(${shadeId}, 'prog', 1);">Prog</button>`
        html += `<button id="btnMarkPaired" type="button" style="padding-left:20px;padding-right:20px;display:inline-block;" onclick="somfy.setPaired(${shadeId}, false);">Shade Unpaired</button>`
        html += `<button id="btnStopUnpairing" type="button" style="padding-left:20px;padding-right:20px;display:inline-block" onclick="document.getElementById('divPairing').remove();">Close</button>`
        html += `</div>`;
        div.innerHTML = html;
        document.getElementById('somfyShade').appendChild(div);
        return div;
    };
    sendCommand(shadeId, command, repeat) {
        console.log(`Sending Shade command ${shadeId}-${command}`);
        let obj = { shadeId: shadeId };
        if (isNaN(parseInt(command, 10))) obj.command = command;
        else obj.target = parseInt(command, 10);
        if (typeof repeat === 'number') obj.repeat = parseInt(repeat);
        putJSON('/shadeCommand', obj, (err, shade) => {

        });
    };
    sendTiltCommand(shadeId, command) {
        console.log(`Sending Tilt command ${shadeId}-${command}`);
        if (isNaN(parseInt(command, 10)))
            putJSON('/tiltCommand', { shadeId: shadeId, command: command }, (err, shade) => {
            });
        else
            putJSON('/tiltCommand', { shadeId: shadeId, target: parseInt(command, 10) }, (err, shade) => {
            });
    };
    linkRemote(shadeId) {
        let div = document.createElement('div');
        let html = `<div id="divLinking" class="instructions" data-type="link-remote" data-shadeid="${shadeId}">`;
        html += '<div>Press any button on the remote to link it to this shade.  This will not change the pairing for the remote and this screen will close when the remote is detected.</div>';
        html += '<hr></hr>';
        html += `<div><div class="button-container"><button id="btnStopLinking" type="button" style="padding-left:20px;padding-right:20px;" onclick="document.getElementById('divLinking').remove();">Cancel</button></div>`;
        html += '</div>';
        div.innerHTML = html;
        document.getElementById('somfyShade').appendChild(div);
        return div;
    };
    unlinkRemote(shadeId, remoteAddress) {
        let prompt = promptMessage(document.getElementById('fsSomfySettings'), 'Are you sure you want to unlink this remote from the shade?', () => {
            let obj = {
                shadeId: shadeId,
                remoteAddress: remoteAddress
            };
            let overlay = waitMessage(prompt);
            putJSON('/unlinkRemote', obj, (err, shade) => {
                console.log(shade);
                overlay.remove();
                prompt.remove();
                this.setLinkedRemotesList(shade);
            });

        });
    };
    deviationChanged(el) {
        document.getElementById('spanDeviation').innerText = (el.value / 100).fmt('#,##0.00');
    };
    rxBandwidthChanged(el) {
        document.getElementById('spanRxBandwidth').innerText = (el.value / 100).fmt('#,##0.00');
    };
    frequencyChanged(el) {
        document.getElementById('spanFrequency').innerText = (el.value / 1000).fmt('#,##0.000');
    }
    txPowerChanged(el) {
        console.log(el.value);
        let lvls = [-30, -20, -15, -10, -6, 0, 5, 7, 10, 11, 12];
        document.getElementById('spanTxPower').innerText = lvls[el.value];
    };
    stepSizeChanged(el) {
        document.getElementById('spanStepSize').innerText = parseInt(el.value, 10).fmt('#,##0');
    };

    processShadeTarget(el, shadeId) {
        let positioner = document.querySelector(`.shade-positioner[data-shadeid="${shadeId}"]`);
        if (positioner) {
            positioner.querySelector(`.shade-target`).innerHTML = el.value;
            somfy.sendCommand(shadeId, el.value);
        }
    }
    processShadeTiltTarget(el, shadeId) {
        let positioner = document.querySelector(`.shade-positioner[data-shadeid="${shadeId}"]`);
        if (positioner) {
            positioner.querySelector(`.shade-tilt-target`).innerHTML = el.value;
            somfy.sendTiltCommand(shadeId, el.value);
        }
    }
    openSetPosition(shadeId) {
        console.log('Opening Shade Positioner');
        if (typeof shadeId === 'undefined') {
            return;
        }
        else {
            let shade = document.querySelector(`div.somfyShadeCtl[data-shadeid="${shadeId}"]`);
            if (shade) {
                let ctls = document.querySelectorAll('.shade-positioner');
                for (let i = 0; i < ctls.length; i++) {
                    console.log('Closing shade positioner');
                    ctls[i].remove();
                }

                let currPos = parseInt(shade.getAttribute('data-target'), 0);
                let elname = shade.querySelector(`.shadectl-name`);
                let shadeName = elname.innerHTML;
                let html = `<div class="shade-name">${shadeName}</div>`;
                html += `<input id="slidShadeTarget" name="shadeTarget" type="range" min="0" max="100" step="1" value="${currPos}" onchange="somfy.processShadeTarget(this, ${shadeId});" oninput="document.getElementById('spanShadeTarget').innerHTML = this.value;" />`;
                html += `<label for="slidShadeTarget"><span>Target Position </span><span><span id="spanShadeTarget" class="shade-target">${currPos}</span><span>%</span></span></label>`;
                if (makeBool(shade.getAttribute('data-tilt'))) {
                    let currTiltPos = parseInt(shade.getAttribute('data-tilttarget'), 10);
                    html += `<input id="slidShadeTiltTarget" name="shadeTarget" type="range" min="0" max="100" step="1" value="${currTiltPos}" onchange="somfy.processShadeTiltTarget(this, ${shadeId});" oninput="document.getElementById('spanShadeTiltTarget').innerHTML = this.value;" />`;
                    html += `<label for="slidShadeTiltTarget"><span>Target Tilt Position </span><span><span id="spanShadeTiltTarget" class="shade-tilt-target">${currTiltPos}</span><span>%</span></span></label>`;
                }
                html += `</div>`;
                let div = document.createElement('div');
                div.setAttribute('class', 'shade-positioner');
                div.setAttribute('data-shadeid', shadeId);
                div.addEventListener('onclick', (event) => { event.stopPropagation(); });
                div.innerHTML = html;
                shade.appendChild(div);
                document.body.addEventListener('click', () => {
                    let ctls = document.querySelectorAll('.shade-positioner');
                    for (let i = 0; i < ctls.length; i++) {
                        console.log('Closing shade positioner');
                        ctls[i].remove();
                    }
                }, { once: true });
            }
        }
    }
};
var somfy = new Somfy();
class MQTT {
    async init() { this.loadMQTT(); }
    async loadMQTT() {
        let overlay = waitMessage(document.getElementById('fsMQTTSettings'));
        getJSON('/mqttsettings', (err, settings) => {
            overlay.remove();
            if (err) {
                console.log(err);
            }
            else {
                console.log(settings);
                let dd = document.getElementsByName('mqtt-protocol')[0];
                for (let i = 0; i < dd.options.length; i++) {
                    if (dd.options[i].text === settings.proto) {
                        dd.selectedIndex = i;
                        break;
                    }
                }
                if (dd.selectedIndex < 0) dd.selectedIndex = 0;
                document.getElementsByName('mqtt-host')[0].value = settings.hostname;
                document.getElementsByName('mqtt-port')[0].value = settings.port;
                document.getElementsByName('mqtt-username')[0].value = settings.username;
                document.getElementsByName('mqtt-password')[0].value = settings.password;
                document.getElementsByName('mqtt-topic')[0].value = settings.rootTopic;
                document.getElementsByName('mqtt-enabled')[0].checked = settings.enabled;
            }
        });
    };
    connectMQTT() {
        if (document.getElementById('btnConnectMQTT').classList.contains('disabled')) return;
        document.getElementById('btnConnectMQTT').classList.add('disabled');
        let obj = {
            enabled: document.getElementsByName('mqtt-enabled')[0].checked,
            protocol: document.getElementsByName('mqtt-protocol')[0].value,
            hostname: document.getElementsByName('mqtt-host')[0].value,
            port: parseInt(document.getElementsByName('mqtt-port')[0].value, 10),
            username: document.getElementsByName('mqtt-username')[0].value,
            password: document.getElementsByName('mqtt-password')[0].value,
            rootTopic: document.getElementsByName('mqtt-topic')[0].value

        }
        console.log(obj);
        if (isNaN(obj.port) || obj.port < 0) {
            errorMessage(document.getElementById('fsMQTTSettings'), 'Invalid port number.  Likely ports are 1183, 8883 for MQTT/S or 80,443 for HTTP/S');
            return;
        }
        let overlay = waitMessage(document.getElementById('fsMQTTSettings'));
        putJSON('/connectmqtt', obj, (err, response) => {
            overlay.remove();
            document.getElementById('btnConnectMQTT').classList.remove('disabled');
            console.log(response);
        });
    };
};
var mqtt = new MQTT();
class Firmware {
    async init() { }
    backup() {
        var link = document.createElement('a');
        link.href = '/backup';
        link.setAttribute('download', 'backup');
        document.body.appendChild(link);
        link.click();
        link.remove();
    };
    restore() {
        let div = this.createFileUploader('/restore');
        let inst = div.querySelector('div[id=divInstText]');
        inst.innerHTML = '<div style="font-size:14px;margin-bottom:20px;">Select a backup file that you would like to restore then press the Upload File button.</div>';
        document.getElementById('fsUpdates').appendChild(div);
    };
    createFileUploader(service) {
        let div = document.createElement('div');
        div.setAttribute('id', 'divUploadFile');
        div.setAttribute('class', 'instructions');
        div.style.width = '100%';
        let html = `<div style="width:100%;text-align:center;"><form method="POST" action="#" enctype="multipart/form-data" id="frmUploadApp" style="">`;
        html += `<div id="divInstText"></div>`;
        html += `<input id="fileName" type="file" name="updateFS" style="display:none;" onchange="document.getElementById('span-selected-file').innerText = this.files[0].name;"/>`;
        html += `<label for="fileName">`;
        html += `<span id="span-selected-file" style="display:inline-block;min-width:257px;border-bottom:solid 2px white;font-size:14px;white-space:nowrap;overflow:hidden;max-width:320px;text-overflow:ellipsis;"></span>`;
        html += `<div id="btn-select-file" class="button-outline" style="font-size:.8em;padding:10px;"><i class="icss-upload" style="margin:0px;"></i></div>`;
        html += `</label>`;
        html += `<div class="progress-bar" id="progFileUpload" style="--progress:0%;margin-top:10px;display:none;"></div>`
        html += `<div class="button-container">`
        html += `<button id="btnUploadFile" type="button" style="width:auto;padding-left:20px;padding-right:20px;margin-right:4px;display:inline-block;" onclick="firmware.uploadFile('${service}', document.getElementById('divUploadFile'));">Upload File</button>`
        html += `<button id="btnClose" type="button" style="width:auto;padding-left:20px;padding-right:20px;display:inline-block;" onclick="document.getElementById('divUploadFile').remove();">Cancel</button></div>`;
        html += `</form><div>`;
        div.innerHTML = html;
        return div;
    };
    updateFirmware() {
        let div = this.createFileUploader('/updateFirmware');
        let inst = div.querySelector('div[id=divInstText]');
        inst.innerHTML = '<div style="font-size:14px;margin-bottom:20px;">Select a binary file [SomfyController.ino.esp32.bin] containing the device firmware then press the Upload File button.</div>';
        document.getElementById('fsUpdates').appendChild(div);
    };
    updateApplication() {
        let div = this.createFileUploader('/updateApplication');
        general.reloadApp = true;
        let inst = div.querySelector('div[id=divInstText]');
        inst.innerHTML = '<div style="font-size:14px;">Select a binary file [SomfyController.littlefs.bin] containing the littlefs data for the application then press the Upload File button.</div>';
        inst.innerHTML += '<hr/><div style="font-size:14px;margin-bottom:10px;">A backup file for your configuration will be downloaded to your browser.  If the application update process fails please restore this file using the restore button</div>';
        document.getElementById('fsUpdates').appendChild(div);
    };
    async uploadFile(service, el) {
        let field = el.querySelector('input[type="file"]');
        let filename = field.value;
        console.log(filename);
        switch (service) {
            case '/updateApplication':
                if (filename.indexOf('.littlefs') === -1 || !filename.endsWith('.bin')) {
                    errorMessage(el, 'This file is not a valid littleFS file system.');
                    return;
                }
                // The first thing we need to do is backup the configuration. So lets do this
                // in a promise.
                await new Promise((resolve, reject) => {
                    firmware.backup();
                    try {
                        // Next we need to download the current configuration data.
                        getText('/shades.cfg', (err, cfg) => {
                            if (err)
                                reject(err);
                            else {
                                resolve();
                                console.log(cfg);
                            }
                        });
                    } catch (err) {
                        reject(err);
                        serviceError(el, err);
                        return;
                    }
                }).catch((err) => {
                    serviceError(el, err);
                });
                break;
            case '/updateFirmware':
                if (filename.indexOf('.ino.') === -1 || !filename.endsWith('.bin')) {
                    errorMessage(el, 'This file is not a valid firmware binary file.');
                    return;
                }
                break;
            case '/restore':
                if (!filename.endsWith('.backup') || filename.indexOf('ESPSomfyRTS') === -1) {
                    errorMessage(el, 'This file is not a valid backup file')
                    return;
                }

        }
        let formData = new FormData();
        let btnUpload = el.querySelector('button[id="btnUploadFile"]');
        let btnCancel = el.querySelector('button[id="btnClose"]');
        btnUpload.style.display = 'none';
        field.disabled = true;
        let btnSelectFile = el.querySelector('div[id="btn-select-file"]');
        let prog = el.querySelector('div[id="progFileUpload"]');
        prog.style.display = '';
        btnSelectFile.style.visibility = 'hidden';
        formData.append('file', field.files[0]);
        let xhr = new XMLHttpRequest();
        xhr.open('POST', service, true);
        xhr.upload.onprogress = function (evt) {
            let pct = evt.total ? Math.round((evt.loaded / evt.total) * 100) : 0;
            prog.style.setProperty('--progress', `${pct}%`);
            prog.setAttribute('data-progress', `${pct}%`);
            console.log(evt);
            
        };
        xhr.onerror = function (err) {
            console.log(err);
        };
        xhr.onload = function () {
            console.log('File upload load called');
            btnCancel.innerText = 'Close';
            switch (service) {
                case '/restore':
                    (async () => {
                        await somfy.init();
                        if (document.getElementById('divUploadFile')) document.getElementById('divUploadFile').remove();
                    })();
                    break;
                case '/updateApplication':

                    break;
            }
        };
        xhr.send(formData);
    };
};
var firmware = new Firmware();
