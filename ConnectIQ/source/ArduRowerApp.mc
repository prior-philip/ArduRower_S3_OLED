import Toybox.Application;
import Toybox.Lang;
import Toybox.WatchUi;

class ArduRowerApp extends Application.AppBase {

    hidden var _ble as RowerBle?;
    hidden var _view as ArduRowerView?;

    function initialize() {
        AppBase.initialize();
    }

    function onStart(state as Dictionary?) as Void {
    }

    function onStop(state as Dictionary?) as Void {
        var b = _ble;
        if (b != null) {
            b.stop();
        }
    }

    function getInitialView() {
        var ble = new RowerBle();
        ble.start();
        _ble = ble;
        var view = new ArduRowerView(ble);
        _view = view;
        return [ view ];
    }
}
