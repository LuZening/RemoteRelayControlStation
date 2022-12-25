
// retrieve current configurations
function get_config() {
    var xhttp = new XMLHttpRequest();
    xhttp.onreadystatechange = function () {
        s_recv = this.responseText;
        if (this.status == 200 && s_recv.length > 2) {
            // parse the recieved text
            // name=%s&ssid=%s&ip=%s&pot_type=%d&ADC_min=%d&ADC_max=%d&degree=%d
            /* Parse received XEncoding String */
            // split by &
            Tokens = s_recv.split("&");
            for(i = 0; i < Tokens.length; i++)
            {
                // Name=Value
                sToken = Tokens[i];
                SubTokens = sToken.split("=");
                if(SubTokens.length == 2)
                {
                    sName = SubTokens[0];
                    sValue = SubTokens[1];
                    // Elements are named by the same name with arguments in the received string
                    Elements = document.getElementsByName(sName);
                    // only display the value if the element exists
                    if(Elements.length > 0)
                    {
                        if(Elements[0].type == "number")
                        {
                            Elements[0].value = parseInt(sValue);
                        }
                        else if(Elements[0].type == "radio")
                        {
                            chosen = parseInt(sValue); // response value starts from 1, while the radio index starts from 0
                            if(chosen < Elements.length && chosen >= 0)
                                Elements[chosen].checked = true;
                        }
                        else if(Elements[0].type == "text")
                        {
                            Elements[0].value = sValue;
                        }
                        else
                        {
                            Elements[0].value = parseInt(sValue);
                        }
                        Elements[0].dispatchEvent(new Event("input"));
                    }
                }
            }
        }
    }
    xhttp.open("GET", "getconfig", true);
    xhttp.setRequestHeader("Content-type", "application/x-www-form-urlencoded");
    xhttp.setRequestHeader("Connection", "Close");
    xhttp.send();
}

