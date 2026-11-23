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

// value ids for the small/half layouts
const V_TIME = 0;
const V_DIST = 1;
const V_PACE = 2;
const V_SPM = 3;
const V_POWER = 4;

class ArduRowerView extends WatchUi.DataField {

    hidden var _ble as RowerBle;

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
        var flags = getObscurityFlags();
        if (w >= 180 && h >= 180) {
            _drawFull(dc, w, h);
        } else if (w >= 180 && h >= 90) {
            _drawHalf(dc, w, h, flags, fg);
        } else {
            _drawSmall(dc, w, h, flags);
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

    hidden function _label(id as Number) as String {
        if (id == V_TIME) { return "ROW TIME"; }
        if (id == V_DIST) { return "METERS"; }
        if (id == V_SPM) { return "SPM"; }
        if (id == V_POWER) { return "WATT"; }
        return "/500M";
    }

    hidden function _text(id as Number) as String {
        if (id == V_TIME) { return _fmtTime(_val(_ble.elapsed)); }
        if (id == V_DIST) { return _fmtNum(_val(_ble.distance)); }
        if (id == V_SPM) { return _fmtSpm(_valF(_ble.strokeRate)); }
        if (id == V_POWER) { return _fmtNum(_val(_ble.power)); }
        return _fmtTime(_val(_ble.pace));
    }

    // label on top, value in the biggest number font that fits the cell
    hidden function _drawCell(dc as Graphics.Dc, cx as Number, top as Number, cw as Number, ch as Number,
                              label as String, text as String) as Void {
        var cc = Graphics.TEXT_JUSTIFY_CENTER | Graphics.TEXT_JUSTIFY_VCENTER;
        var lblFont = Graphics.FONT_XTINY;
        var lblH = dc.getFontHeight(lblFont);
        dc.drawText(cx, top + lblH / 2, lblFont, label, cc);

        var fonts = [Graphics.FONT_NUMBER_HOT, Graphics.FONT_NUMBER_MEDIUM, Graphics.FONT_NUMBER_MILD, Graphics.FONT_MEDIUM];
        var font = Graphics.FONT_SMALL;
        var avail = ch - lblH;
        for (var i = 0; i < fonts.size(); i++) {
            var f = fonts[i] as Graphics.FontType;
            if (dc.getFontHeight(f) * 85 / 100 <= avail && dc.getTextWidthInPixels(text, f) <= cw - 4) {
                font = f;
                break;
            }
        }
        dc.drawText(cx, top + lblH + avail / 2, font, text, cc);
    }

    // which value a small slot shows, from its position on the screen
    //   top -> time, middle -> pace, bottom -> stroke rate
    //   quadrants: top-left time, top-right meters, bottom-left spm, bottom-right watts
    hidden function _smallValue(flags as Number) as Number {
        var t = (flags & WatchUi.OBSCURE_TOP) != 0;
        var b = (flags & WatchUi.OBSCURE_BOTTOM) != 0;
        var l = (flags & WatchUi.OBSCURE_LEFT) != 0;
        var r = (flags & WatchUi.OBSCURE_RIGHT) != 0;
        var leftOnly = l && !r;
        var rightOnly = r && !l;
        if (t && !b) {
            return rightOnly ? V_DIST : V_TIME;
        }
        if (b && !t) {
            return rightOnly ? V_POWER : V_SPM;
        }
        if (leftOnly) {
            return V_PACE;
        }
        if (rightOnly) {
            return V_SPM;
        }
        return V_PACE;
    }

    hidden function _drawSmall(dc as Graphics.Dc, w as Number, h as Number, flags as Number) as Void {
        var id = _smallValue(flags);
        var label = _label(id);
        var status = _status();
        if (status.length() > 0) {
            label = status;
        }
        // keep away from the round bezel at the top/bottom edge
        var inset = 0;
        if ((flags & WatchUi.OBSCURE_TOP) != 0 && (flags & WatchUi.OBSCURE_BOTTOM) == 0) {
            inset = h / 8;
        }
        var bottomCut = ((flags & WatchUi.OBSCURE_BOTTOM) != 0 && (flags & WatchUi.OBSCURE_TOP) == 0) ? h / 8 : 0;
        _drawCell(dc, w / 2, inset, w, h - inset - bottomCut, label, _text(id));
    }

    // wide half-screen slot: 2x2 grid
    //   top half:    time | meters  /  pace | spm
    //   bottom half: pace | spm     /  watts | meters
    hidden function _drawHalf(dc as Graphics.Dc, w as Number, h as Number, flags as Number, fg as Number) as Void {
        var bottomHalf = (flags & WatchUi.OBSCURE_BOTTOM) != 0 && (flags & WatchUi.OBSCURE_TOP) == 0;
        var ids = bottomHalf ? [V_PACE, V_SPM, V_POWER, V_DIST] : [V_TIME, V_DIST, V_PACE, V_SPM];

        // round screen: the outer edge of a half is narrow, so leave some room there
        var edge = h / 8;
        var top = bottomHalf ? 0 : edge;
        var rowH = (h - edge) / 2;
        var cw = w * 36 / 100;
        var xl = w * 32 / 100;
        var xr = w * 68 / 100;

        var status = _status();
        for (var i = 0; i < 4; i++) {
            var id = ids[i] as Number;
            var label = _label(id);
            if (i == 0 && status.length() > 0) {
                label = status;
            }
            _drawCell(dc, (i % 2 == 0) ? xl : xr, top + (i / 2) * rowH, cw, rowH, label, _text(id));
        }
        dc.setColor(fg, Graphics.COLOR_TRANSPARENT);
        dc.drawLine(w / 2, top + 4, w / 2, top + 2 * rowH - 4);
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
