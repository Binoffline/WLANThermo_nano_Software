(function (window, document) {

    var layout   = document.getElementById('layout'),
        menu     = document.getElementById('menu'),
        menuLink = document.getElementById('menuLink'),
		menuHome = document.getElementById('menuHome'),
		menuPlot = document.getElementById('menuPlot'),
		menuWlan = document.getElementById('menuWlan'),
		menuSystem = document.getElementById('menuSystem'),
		menuPitmaster = document.getElementById('menuPitmaster'),
		menuChart = document.getElementById('menuChart'),
		menuChannel = document.getElementById('menuChannel'),
		menuAbout = document.getElementById('menuAbout'),
        content  = document.getElementById('main');
    function toggleClass(element, className) {
        var classes = element.className.split(/\s+/),
            length = classes.length,
            i = 0;

        for(; i < length; i++) {
          if (classes[i] === className) {
            classes.splice(i, 1);
            break;
          }
        }
        // The className is not found
        if (length === classes.length) {
            classes.push(className);
        }

        element.className = classes.join(' ');
    }

    function toggleAll(e) {
        var active = 'active';
        e.preventDefault();
        toggleClass(layout, active);
        toggleClass(menu, active);
        toggleClass(menuLink, active);
    }

    menuLink.onclick = function (e) {
        toggleAll(e);
    };
    menuHome.onclick = function (e) {
        if (menu.className.indexOf('active') !== -1) {
            toggleAll(e);
			showIndex();
        }
    };
    menuPlot.onclick = function (e) {
        if (menu.className.indexOf('active') !== -1) {
            toggleAll(e);
			showPlot();
        }
    };
    menuWlan.onclick = function (e) {
        if (menu.className.indexOf('active') !== -1) {
            toggleAll(e);
			showWLAN();
        }
    };
    menuSystem.onclick = function (e) {
        if (menu.className.indexOf('active') !== -1) {
            toggleAll(e);
			showSystem();
        }
    };
    menuPitmaster.onclick = function (e) {
        if (menu.className.indexOf('active') !== -1) {
            toggleAll(e);
			showPitmaster();
        }
    };
    menuChart.onclick = function (e) {
        if (menu.className.indexOf('active') !== -1) {
            toggleAll(e);
			showChart();
        }
    };
    menuChannel.onclick = function (e) {
        if (menu.className.indexOf('active') !== -1) {
            toggleAll(e);
			showChannel();
        }
    };
    menuAbout.onclick = function (e) {
        if (menu.className.indexOf('active') !== -1) {
            toggleAll(e);
			showAbout();
        }
    };
 
    content.onclick = function(e) {
        if (menu.className.indexOf('active') !== -1) {
            toggleAll(e);
        }
    };

}(this, this.document));