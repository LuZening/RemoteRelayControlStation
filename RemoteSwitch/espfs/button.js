// detect if the browser supports touch events
const buttonPressEvent = (
    function ()
    {
        if (document.documentElement.ontouchstart !== undefined)
            return 'touchstart';
        else
            return 'mousedown'
    }
)();


const buttonReleaseEvent = (
    function ()
    {
        if (document.documentElement.ontouchend !== undefined)
            return 'touchend';
        else
            return 'mouseup';
    }
)();