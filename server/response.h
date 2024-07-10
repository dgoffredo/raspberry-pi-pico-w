// Biggest uint32: 4294967295 (length 10)
// Biggest uint16: 65535 (length 5)
// Smallest int32: -2147483648 (length 11)
// with a decimal point, that's -2147483.648 (length 12)
// The JSON without the values has length 90.
// Then it's:
// - another 5 for the sequence number
// - another 12 for the temperature
// - another 12 for the humidity
// So, Content-Length is 90+5+12+12 = 119.
// So, the entire response length is 119+96+(2*5) = 119+106 = 225

// const char response_format[] =
"HTTP/1.1 200 OK\r\n"
"Connection: close\r\n"
"Content-Type: application/json; charset=utf-8\r\n"
"Content-Length: 119\r\n"
"\r\n"
"{\"sequence_number\": %10lu, \"CO2_ppm\": %5hu, \"temperature_celsius\": %8ld.%03ld, \"relative_humidity_percent\": %8ld.%03ld}"

// char buffer[106+1];
// snprintf(buffer, sizeof buffer, response_format, seq, co2, temp/1000, abs(temp)%1000, humid/1000, abs(humid)%1000);
