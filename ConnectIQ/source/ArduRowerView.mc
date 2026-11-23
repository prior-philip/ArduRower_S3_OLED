import Toybox.Activity;
import Toybox.Application;
import Toybox.FitContributor;
import Toybox.Graphics;
import Toybox.Lang;
import Toybox.WatchUi;

// FIT field ids (must match resources/fit/fit_contributions.xml)
const FIT_SPM = 0;
const FIT_POWER = 1;
const FIT_PACE = 2;
const FIT_DIST = 3;
const FIT_STROKES = 4;
const FIT_AVG_SPM = 5;
const FIT_AVG_POWER = 6;
const FIT_AVG_PACE = 7;
const FIT_TOTAL_DIST = 8;
const FIT_TOTAL_STROKES = 9;

class ArduRowerView extends WatchUi.DataField {

    hidden var _ble as RowerBle;
    hidden var _smallField as Number = 0;   // 0 pace, 1 power, 2 spm, 3 distance, 4 time

    // FIT record fields (charts)
    hidden var _fSpm as FitContributor.Field;
    hidden var _fPower as FitContributor.Field;
    hidden var _fPace as FitContributor.Field;
    hidden var _fDist as FitContributor.Field;
    hidden var _fStrokes as FitContributor.Field;
    // FIT session fields (activity summary)
    hidden var _fAvgSpm as FitContributor.Field;
    hidden var _fAvgPower as FitContributor.Field;
    hidden var _fAvgPace as FitContributor.Field;
    hidden var _fTotalDist as FitContributor.Field;
    hidden var _fTotalStrokes as FitContributor.Field;

    function initialize(ble as RowerBle) {
        DataField.initialize();
        _ble = ble;
        loadSettings();

        var rec = FitContributor.MESG_TYPE_RECORD;
        var ses = FitContributor.MESG_TYPE_SESSION;
        _fSpm     = createField("stroke_rate", FIT_SPM, FitContributor.DATA_TYPE_FLOAT, { :mesgType => rec, :units => "spm" });
        _fPower   = createField("power", FIT_POWER, FitContributor.DATA_TYPE_UINT16, { :mesgType => rec, :units => "W" });
        _fPace    = createField("pace_500m", FIT_PACE, FitContributor.DATA_TYPE_UINT16, { :mesgType => rec, :units => "s" });
        _fDist    = createField("row_distance", FIT_DIST, FitContributor.DATA_TYPE_UINT32, { :mesgType => rec, :units => "m" });
        _fStrokes = createField("strokes", FIT_STROKES, FitContributor.DATA_TYPE_UINT16, { :mesgType => rec, :units => "strokes" });

        _fAvgSpm       = createField("avg_stroke_rate", FIT_AVG_SPM, FitContributor.DATA_TYPE_FLOAT, { :mesgType => ses, :units => "spm" });
        _fAvgPower     = createField("avg_power", FIT_AVG_POWER, FitContributor.DATA_TYPE_UINT16, { :mesgType => ses, :units => "W" });
        _fAvgPace      = createField("avg_pace_500m", FIT_AVG_PACE, FitContributor.DATA_TYPE_UINT16, { :mesgType => ses, :units => "s" });
        _fTotalDist    = createField("total_row_distance", FIT_TOTAL_DIST, FitContributor.DATA_TYPE_UINT32, { :mesgType => ses, :units => "m" });
        _fTotalStrokes = createField("total_strokes", FIT_TOTAL_STROKES, FitContributor.DATA_TYPE_UINT16, { :mesgType => ses, :units => "strokes" });
    }

    function loadSettings() as Void {
        try {
            var v = Application.Properties.getValue("smallField");
            if (v instanceof Number) {
                _smallField = v;
            }
        } catch (e) {
            _smallField = 0;
        }
    }

    // ~1x per second
    function compute(info as Activity.Info) as Void {
        _ble.tick();
        if (!_ble.isReceiving()) {
            return;
        }
        _set(_fSpm, _ble.strokeRate);
        _set(_fPower, _ble.power);
        _set(_fPace, _ble.pace);
        _set(_fDist, _ble.distance);
        _set(_fStrokes, _ble.strokeCount);

        _set(_fAvgSpm, _ble.avgStrokeRate);
        _set(_fAvgPower, _ble.avgPower);
        _set(_fAvgPace, _ble.avgPace);
        _set(_fTotalDist, _ble.distance);
        _set(_fTotalStrokes, _ble.strokeCount);
    }

    hidden function _set(field as FitContributor.Field, value as Numeric?) as Void {
        if (value != null) {
            field.setData(value);
        }
    }

    function onUpdate(dc as Graphics.Dc) as Void {
        var bg = getBackgroundColor();
        var fg = (bg == Graphics.COLOR_BLACK) ? Graphics.COLOR_WHITE : Graphics.COLOR_BLACK;
        dc.setColor(fg, bg);
        dc.clear();
        dc.setColor(fg, Graphics.COLOR_TRANSPARENT);

        var w = dc.getWidth();
        var h = dc.getHeight();
        if (w >= 180 && h >= 180) {
            _drawFull(dc, w, h);
        } else {
            _drawSmall(dc, w, h);
        }
    }

    // ---------------------------------------------------------------- layouts

    // full screen (280x280 round on the fenix 7X)
    hidden function _drawFull(dc as Graphics.Dc, w as Number, h as Number) as Void {
        var cx = w / 2;
        var cc = Graphics.TEXT_JUSTIFY_CENTER | Graphics.TEXT_JUSTIFY_VCENTER;
        var lbl = Graphics.FONT_XTINY;
        var big = Graphics.FONT_NUMBER_MEDIUM;
        var mid = Graphics.FONT_NUMBER_MILD;
        var xl = w * 3 / 10;
        var xr = w * 7 / 10;

        // status line
        var status = _status();
        if (status.length() == 0) {
            var b = _ble.battery;
            status = (b != null) ? "Rower " + b.format("%d") + "%" : "Rower";
        }
        dc.drawText(cx, h * 12 / 100, lbl, status, cc);

        // rowing time
        dc.drawText(cx, h * 28 / 100, big, _fmtTime(_ble.isReceiving() ? _ble.elapsed : null), cc);

        // distance | pace
        dc.drawText(xl, h * 44 / 100, lbl, "METERS", cc);
        dc.drawText(xr, h * 44 / 100, lbl, "/500M", cc);
        dc.drawText(xl, h * 55 / 100, mid, _fmtNum(_val(_ble.distance)), cc);
        dc.drawText(xr, h * 55 / 100, mid, _fmtTime(_val(_ble.pace)), cc);

        dc.drawLine(w / 10, h * 63 / 100, w * 9 / 10, h * 63 / 100);

        // stroke rate | power
        dc.drawText(xl, h * 69 / 100, lbl, "SPM", cc);
        dc.drawText(xr, h * 69 / 100, lbl, "WATT", cc);
        dc.drawText(xl, h * 80 / 100, mid, _fmtSpm(_valF(_ble.strokeRate)), cc);
        dc.drawText(xr, h * 80 / 100, mid, _fmtNum(_val(_ble.power)), cc);
    }

    // one value, selected in the data field settings (Connect IQ app)
    hidden function _drawSmall(dc as Graphics.Dc, w as Number, h as Number) as Void {
        var cx = w / 2;
        var cc = Graphics.TEXT_JUSTIFY_CENTER | Graphics.TEXT_JUSTIFY_VCENTER;
        var lblFont = Graphics.FONT_XTINY;
        var lblH = dc.getFontHeight(lblFont);

        var label;
        var text;
        if (_smallField == 1) {
            label = "WATT";
            text = _fmtNum(_val(_ble.power));
        } else if (_smallField == 2) {
            label = "SPM";
            text = _fmtSpm(_valF(_ble.strokeRate));
        } else if (_smallField == 3) {
            label = "METERS";
            text = _fmtNum(_val(_ble.distance));
        } else if (_smallField == 4) {
            label = "ROW TIME";
            text = _fmtTime(_val(_ble.elapsed));
        } else {
            label = "/500M";
            text = _fmtTime(_val(_ble.pace));
        }

        var status = _status();
        if (status.length() > 0) {
            label = status;
        }
        dc.drawText(cx, lblH / 2 + 2, lblFont, label, cc);

        // biggest number font that fits below the label
        var fonts = [Graphics.FONT_NUMBER_HOT, Graphics.FONT_NUMBER_MEDIUM, Graphics.FONT_NUMBER_MILD, Graphics.FONT_MEDIUM];
        var font = Graphics.FONT_SMALL;
        var avail = h - lblH;
        for (var i = 0; i < fonts.size(); i++) {
            var f = fonts[i] as Graphics.FontType;
            if (dc.getFontHeight(f) <= avail && dc.getTextWidthInPixels(text, f) <= w - 4) {
                font = f;
                break;
            }
        }
        dc.drawText(cx, lblH + avail / 2, font, text, cc);
    }

    // ---------------------------------------------------------------- helpers

    hidden function _status() as String {
        var s = _ble.state;
        if (s == ST_INIT || s == ST_SCANNING) {
            return "Searching rower";
        } else if (s == ST_CONNECTING || s == ST_SUBSCRIBING) {
            return "Connecting";
        } else if (s == ST_LOST) {
            return "Reconnecting";
        } else if (!_ble.isReceiving()) {
            return "No data";
        }
        return "";
    }

    hidden function _val(v as Number?) as Number? {
        return _ble.isReceiving() ? v : null;
    }

    hidden function _valF(v as Float?) as Float? {
        return _ble.isReceiving() ? v : null;
    }

    hidden function _fmtNum(v as Number?) as String {
        return (v == null) ? "--" : v.format("%d");
    }

    hidden function _fmtSpm(v as Float?) as String {
        return (v == null) ? "--" : v.format("%.0f");
    }

    // seconds -> m:ss or h:mm:ss
    hidden function _fmtTime(s as Number?) as String {
        if (s == null) {
            return "--:--";
        }
        var hrs = s / 3600;
        var min = (s % 3600) / 60;
        var sec = s % 60;
        if (hrs > 0) {
            return hrs.format("%d") + ":" + min.format("%02d") + ":" + sec.format("%02d");
        }
        return min.format("%d") + ":" + sec.format("%02d");
    }
}
