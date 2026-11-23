// BLE client for the ArduRower ESP32 (FTMS Rower Data, service 0x1826)
//
// Flow: register profile -> scan (match by service UUID 0x1826 or name "WR S4BL3",
// also parsed from the raw advertisement) -> pair/connect -> enable notifications on
// Rower Data (0x2AD1) and Battery Level (0x2A19, located inside the FTMS service in the
// ArduRower firmware) -> decode notifications.

import Toybox.BluetoothLowEnergy;
import Toybox.Lang;
import Toybox.System;

const ROWER_NAME = "WR S4BL3";

enum {
    ST_INIT,          // waiting for profile registration
    ST_SCANNING,
    ST_CONNECTING,    // pairDevice() called, waiting for the connection
    ST_SUBSCRIBING,   // enabling notifications
    ST_CONNECTED,
    ST_LOST           // connection dropped, waiting for the system to reconnect
}

class RowerBle extends BluetoothLowEnergy.BleDelegate {

    var state as Number = ST_INIT;
    var device as BluetoothLowEnergy.Device? = null;
    var lastDataMs as Number = 0;

    // decoded values (null = not received yet / not valid)
    var strokeRate as Float? = null;       // strokes/min
    var strokeCount as Number? = null;
    var avgStrokeRate as Float? = null;    // strokes/min
    var distance as Number? = null;        // m
    var pace as Number? = null;            // s per 500 m (null when standing still)
    var avgPace as Number? = null;         // s per 500 m
    var power as Number? = null;           // W
    var avgPower as Number? = null;        // W
    var elapsed as Number? = null;         // s (moving time from the rower)
    var battery as Number? = null;         // %

    hidden var _ftms as BluetoothLowEnergy.Uuid;
    hidden var _rowerData as BluetoothLowEnergy.Uuid;
    hidden var _battery as BluetoothLowEnergy.Uuid;
    hidden var _stateSince as Number = 0;
    hidden var _pending as Array = [];
    hidden var _pendingIdx as Number = 0;

    function initialize() {
        BleDelegate.initialize();
        _ftms      = BluetoothLowEnergy.stringToUuid("00001826-0000-1000-8000-00805f9b34fb");
        _rowerData = BluetoothLowEnergy.stringToUuid("00002ad1-0000-1000-8000-00805f9b34fb");
        _battery   = BluetoothLowEnergy.stringToUuid("00002a19-0000-1000-8000-00805f9b34fb");
    }

    function start() as Void {
        BluetoothLowEnergy.setDelegate(self);
        _setState(ST_INIT);
        try {
            BluetoothLowEnergy.registerProfile({
                :uuid => _ftms,
                :characteristics => [
                    { :uuid => _rowerData, :descriptors => [BluetoothLowEnergy.cccdUuid()] },
                    { :uuid => _battery,   :descriptors => [BluetoothLowEnergy.cccdUuid()] }
                ]
            });
        } catch (e) {
            System.println("registerProfile failed: " + e.getErrorMessage());
            _startScan();
        }
    }

    function stop() as Void {
        try {
            BluetoothLowEnergy.setScanState(BluetoothLowEnergy.SCAN_STATE_OFF);
        } catch (e) {
        }
        _unpair();
    }

    // called once per second from DataField.compute(): timeouts / recovery
    function tick() as Void {
        var dt = System.getTimer() - _stateSince;
        if (state == ST_INIT && dt > 3000) {
            _startScan();                                  // profile callback missing
        } else if ((state == ST_CONNECTING || state == ST_SUBSCRIBING) && dt > 15000) {
            System.println("connect timeout");
            _reset();
        } else if (state == ST_LOST && dt > 20000) {
            System.println("no reconnect, scanning again");
            _reset();
        }
    }

    // true while notifications arrive (the rower sends once per second)
    function isReceiving() as Boolean {
        return state == ST_CONNECTED && (System.getTimer() - lastDataMs) < 5000;
    }

    // ---------------------------------------------------------------- BleDelegate

    function onProfileRegister(uuid as BluetoothLowEnergy.Uuid, status as BluetoothLowEnergy.Status) as Void {
        System.println("profile registered, status " + status);
        if (state == ST_INIT) {
            _startScan();
        }
    }

    function onScanResults(scanResults as BluetoothLowEnergy.Iterator) as Void {
        if (state != ST_SCANNING) {
            return;
        }
        for (var r = scanResults.next() as BluetoothLowEnergy.ScanResult?; r != null;
             r = scanResults.next() as BluetoothLowEnergy.ScanResult?) {
            if (_isRower(r)) {
                System.println("rower found, pairing");
                try {
                    BluetoothLowEnergy.setScanState(BluetoothLowEnergy.SCAN_STATE_OFF);
                } catch (e) {
                }
                _setState(ST_CONNECTING);
                try {
                    device = BluetoothLowEnergy.pairDevice(r);
                } catch (e) {
                    System.println("pairDevice failed: " + e.getErrorMessage());
                    device = null;
                }
                if (device == null) {
                    _startScan();
                }
                return;
            }
        }
    }

    function onConnectedStateChanged(dev as BluetoothLowEnergy.Device, cs as BluetoothLowEnergy.ConnectionState) as Void {
        if (cs == BluetoothLowEnergy.CONNECTION_STATE_CONNECTED) {
            System.println("connected");
            device = dev;
            _subscribe(dev);
        } else {
            System.println("disconnected");
            _setState(ST_LOST);
        }
    }

    function onDescriptorWrite(descriptor as BluetoothLowEnergy.Descriptor, status as BluetoothLowEnergy.Status) as Void {
        _writeNext();
    }

    function onCharacteristicChanged(characteristic as BluetoothLowEnergy.Characteristic, value as ByteArray) as Void {
        var u = characteristic.getUuid();
        if (u.equals(_rowerData)) {
            _parseRowerData(value);
        } else if (u.equals(_battery)) {
            if (value.size() > 0) {
                battery = value[0];
            }
        }
    }

    // ---------------------------------------------------------------- internals

    hidden function _setState(s as Number) as Void {
        state = s;
        _stateSince = System.getTimer();
    }

    hidden function _startScan() as Void {
        _setState(ST_SCANNING);
        try {
            BluetoothLowEnergy.setScanState(BluetoothLowEnergy.SCAN_STATE_SCANNING);
        } catch (e) {
            System.println("scan failed: " + e.getErrorMessage());
        }
    }

    hidden function _unpair() as Void {
        var d = device;
        if (d != null) {
            try {
                BluetoothLowEnergy.unpairDevice(d);
            } catch (e) {
            }
        }
        device = null;
    }

    hidden function _reset() as Void {
        _unpair();
        _startScan();
    }

    hidden function _isRower(r as BluetoothLowEnergy.ScanResult) as Boolean {
        var name = r.getDeviceName();
        if (name != null && name.equals(ROWER_NAME)) {
            return true;
        }
        var uuids = r.getServiceUuids();
        if (uuids != null) {
            for (var u = uuids.next(); u != null; u = uuids.next()) {
                if (_ftms.equals(u)) {
                    return true;
                }
            }
        }
        // fallback: some firmware versions return empty parsed fields, look at the raw advertisement
        return _rawHasRower(r.getRawData());
    }

    // parses advertising AD structures [len][type][data...]
    hidden function _rawHasRower(raw as ByteArray?) as Boolean {
        if (raw == null) {
            return false;
        }
        var n = raw.size();
        var nm = ROWER_NAME.toUtf8Array();
        var i = 0;
        while (i + 1 < n) {
            var len = raw[i];
            if (len == 0) {
                break;
            }
            var end = i + 1 + len;                         // exclusive
            if (end > n) {
                break;
            }
            var type = raw[i + 1];
            if (type == 0x02 || type == 0x03) {            // 16-bit service UUIDs
                for (var j = i + 2; j + 1 < end; j += 2) {
                    if (raw[j] == 0x26 && raw[j + 1] == 0x18) {
                        return true;
                    }
                }
            } else if ((type == 0x08 || type == 0x09) && (end - i - 2) == nm.size()) {   // name
                var same = true;
                for (var k = 0; k < nm.size(); k++) {
                    if (raw[i + 2 + k] != nm[k]) {
                        same = false;
                        break;
                    }
                }
                if (same) {
                    return true;
                }
            }
            i = end;
        }
        return false;
    }

    hidden function _subscribe(dev as BluetoothLowEnergy.Device) as Void {
        _setState(ST_SUBSCRIBING);
        _pending = [];
        _pendingIdx = 0;
        var svc = dev.getService(_ftms);
        if (svc == null) {
            System.println("FTMS service not found");
            return;                                        // tick() times out and rescans
        }
        var cccd = BluetoothLowEnergy.cccdUuid();
        var rd = svc.getCharacteristic(_rowerData);
        if (rd != null) {
            var d = rd.getDescriptor(cccd);
            if (d != null) {
                _pending.add(d);
            }
        }
        var bat = svc.getCharacteristic(_battery);
        if (bat != null) {
            var d2 = bat.getDescriptor(cccd);
            if (d2 != null) {
                _pending.add(d2);
            }
        }
        _writeNext();
    }

    // one GATT request at a time: write the next CCCD, the next one follows in onDescriptorWrite()
    hidden function _writeNext() as Void {
        while (_pendingIdx < _pending.size()) {
            var d = _pending[_pendingIdx] as BluetoothLowEnergy.Descriptor;
            _pendingIdx++;
            try {
                d.requestWrite([0x01, 0x00]b);             // enable notifications
                return;
            } catch (e) {
                System.println("CCCD write failed: " + e.getErrorMessage());
            }
        }
        if (state == ST_SUBSCRIBING) {
            _setState(ST_CONNECTED);
        }
    }

    hidden function _u16(v as ByteArray, p as Number) as Number {
        return v[p] | (v[p + 1] << 8);
    }

    hidden function _paceOrNull(p as Number) as Number? {
        return (p == 0 || p == 65535) ? null : p;
    }

    // FTMS Rower Data (0x2AD1). The ArduRower sends it in two parts (flags 0x00FE and 0x1F01)
    // plus a 3-byte keep-alive (flags 0x0001), so this follows the flag bits generically.
    hidden function _parseRowerData(v as ByteArray) as Void {
        var n = v.size();
        if (n < 2) {
            return;
        }
        var flags = _u16(v, 0);
        var p = 2;
        if ((flags & 0x0001) == 0) {                       // "More Data" = 0: stroke rate + count present
            if (p + 3 > n) { return; }
            strokeRate = v[p] / 2.0f;                      // 0.5 /min units
            strokeCount = _u16(v, p + 1);
            p += 3;
        }
        if ((flags & 0x0002) != 0) {                       // average stroke rate
            if (p + 1 > n) { return; }
            avgStrokeRate = v[p] / 2.0f;
            p += 1;
        }
        if ((flags & 0x0004) != 0) {                       // total distance uint24
            if (p + 3 > n) { return; }
            distance = v[p] | (v[p + 1] << 8) | (v[p + 2] << 16);
            p += 3;
        }
        if ((flags & 0x0008) != 0) {                       // instantaneous pace
            if (p + 2 > n) { return; }
            pace = _paceOrNull(_u16(v, p));
            p += 2;
        }
        if ((flags & 0x0010) != 0) {                       // average pace
            if (p + 2 > n) { return; }
            avgPace = _paceOrNull(_u16(v, p));
            p += 2;
        }
        if ((flags & 0x0020) != 0) {                       // instantaneous power sint16
            if (p + 2 > n) { return; }
            var pw = _u16(v, p);
            power = (pw >= 32768) ? pw - 65536 : pw;
            p += 2;
        }
        if ((flags & 0x0040) != 0) {                       // average power sint16
            if (p + 2 > n) { return; }
            var apw = _u16(v, p);
            avgPower = (apw >= 32768) ? apw - 65536 : apw;
            p += 2;
        }
        if ((flags & 0x0080) != 0) { p += 2; }             // resistance level
        if ((flags & 0x0100) != 0) { p += 5; }             // total energy, per hour, per minute
        if ((flags & 0x0200) != 0) { p += 1; }             // heart rate
        if ((flags & 0x0400) != 0) { p += 1; }             // metabolic equivalent
        if ((flags & 0x0800) != 0) {                       // elapsed time
            if (p + 2 > n) { return; }
            elapsed = _u16(v, p);
            p += 2;
        }

        if (flags != 0x0001) {                             // real data, not the keep-alive
            lastDataMs = System.getTimer();
            if (state != ST_CONNECTED) {
                _setState(ST_CONNECTED);
            }
        }
    }
}
