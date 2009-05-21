<?php
/*
 * SimpleID
 *
 * Copyright (C) Kelvin Mo 2007-9
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the Free
 * Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 * 
 * $Id$
 */
 
/**
 * Functions for making and processing HTTP requests.
 *
 * @package simpleid
 * @since 0.7
 * @filesource
 */

/**
 * The user agent to use during HTTP requests.
 */
define('SIMPLEID_USER_AGENT', 'SimpleID/' . SIMPLEID_VERSION);

/**
 * Performs an HTTP request.
 *
 * Communication with the web server is conducted using libcurl where possible.
 * Where libcurl does not exist, then sockets will be used.
 *
 * @param string $url the URL
 * @param array $headers HTTP headers containing name => value pairs
 * @param string $method the HTTP request method
 * @param int $retry the maximum number of redirects allowed
 * @return array containing keys 'error-code' (for communication errors), 'error'
 * (for communication errors), 'data' (content returned), 'code' (the HTTP status code), 'http-error'
 * (if the HTTP status code is not 200 or 304), 'headers' (an array of return headers),
 * 'content-type' (the HTTP content-type returned)
 */
function http_request($url, $headers = array(), $method = 'GET', $retry = 3) {
    // If CURL is available, we use it
    if (extension_loaded('curl')) {
        $response = _http_request_curl($url, $headers, $method, $retry);
    } else {
        $response = _http_request_fsock($url, $headers, $method, $retry);
    }
    
    if (!isset($response['error-code'])) {
        $valid_codes = array(
            100, 101,
            200, 201, 202, 203, 204, 205, 206,
            300, 301, 302, 303, 304, 305, 307,
            400, 401, 402, 403, 404, 405, 406, 407, 408, 409, 410, 411, 412, 413, 414, 415, 416, 417,
            500, 501, 502, 503, 504, 505
        );
    
        // RFC 2616 states that all unknown HTTP codes must be treated the same as the
        // base code in their class.
        if (!in_array($code, $valid_codes)) {
            $code = floor($code / 100) * 100;
        }
        
        if (($code != 200) || ($code != 304)) {
            $response['http-error'] = $code;
        }
    }

    return $response;
}

/**
 * Performs an HTTP request using libcurl.
 *
 * @param string $url the URL
 * @param array $headers HTTP headers containing name => value pairs
 * @param string $method the HTTP request method
 * @param int $retry the maximum number of redirects allowed
 * @return array containing keys 'error-code' (for communication errors), 'error'
 * (for communication errors), 'data' (content returned), 'code' (the HTTP status code), 'http-error'
 * (if the HTTP status code is not 200 or 304), 'headers' (an array of return headers),
 * 'content-type' (the HTTP content-type returned)
 */
function _http_request_curl($url, $headers = array(), $method = 'GET', $retry = 3) {
    $version = curl_version();
    
    $curl = curl_init($url);
    
    if (version_compare($version['version'], '7.10.5', '>=')) {
        curl_setopt($curl, CURLOPT_ENCODING, '');
    }
    
    curl_setopt($curl, CURLOPT_CUSTOMREQUEST, $method);
    curl_setopt($curl, CURLOPT_FOLLOWLOCATION, true);
    curl_setopt($curl, CURLOPT_MAXREDIRS, $retry);
    curl_setopt($curl, CURLOPT_HTTPHEADER, array(implode("\n", $headers) . "\n"));
    curl_setopt($fp, CURLOPT_USERAGENT, SIMPLEID_USER_AGENT);
    
    curl_setopt($fp, CURLOPT_TIMEOUT, 20);
    curl_setopt($fp, CURLOPT_CONNECTTIMEOUT, 20);
    
    curl_setopt($curl, CURLOPT_RETURNTRANSFER, true);
    curl_setopt($curl, CURLOPT_HEADER, true);
    
    curl_setopt($curl, CURLOPT_SSL_VERIFYPEER, false);
    curl_setopt($curl, CURLOPT_SSL_VERIFYHOST, false);
    
    $response = curl_exec($curl);
    
    if (($response === FALSE) && ((curl_errno($curl) == 23) || (curl_errno($curl) == 61))) {
        curl_setopt($curl, CURLOPT_ENCODING, 'none');
        $response = curl_exec($curl);
    }
    
    if ($response === FALSE) {
        $result = array();
        $result['error-code'] = curl_errno($curl);
        $result['error'] = curl_error($curl);
    } else {
        $result['code'] = curl_getinfo($curl, CURLINFO_HTTP_CODE);
        $result['url'] = curl_getinfo($curl, CURLINFO_EFFECTIVE_URL);
        $result['content-type'] = curl_getinfo($curl, CURLINFO_CONTENT_TYPE);
        $result['headers'] = array();
        
        // Parse response.
        $result['raw'] = $response;
        
        $header_size = curl_getinfo($curl, CURLINFO_HEADER_SIZE);
        $result['data'] = substr($response, $header_size);
        
        $response_headers = substr($response, 0, $header_size - 4);

        // In case where redirect occurs, we want the last set of headers
        $header_blocks = explode("\r\n\r\n", $response_headers);
        $header_block = array_pop($header_blocks);

        $result['headers'] = _http_parse_headers($header_block, TRUE);
    }
    
    curl_close($curl);

    return $result;
}

/**
 * Performs an HTTP request using sockets.
 *
 * @param string $url the URL
 * @param array $headers HTTP headers containing name => value pairs
 * @param string $method the HTTP request method
 * @param int $retry the maximum number of redirects allowed
 * @return array containing keys 'error-code' (for communication errors), 'error'
 * (for communication errors), 'data' (content returned), 'code' (the HTTP status code), 'http-error'
 * (if the HTTP status code is not 200 or 304), 'headers' (an array of return headers),
 * 'content-type' (the HTTP content-type returned)
 */
function _http_request_fsock($url, $headers = array(), $method = 'GET', $retry = 3) {
    $result = array();
    
    $parts = parse_url($url);
    
    if (!isset($parts)) {
        $result['error-code'] = 3;
        $result['error'] = 'URL not properly formatted';
        return $result;
    }
    
    if ($parts['scheme'] == 'http') {
        $port = isset($parts['port']) ? $parts['port'] : 80;
        $host = $parts['host'];
    } elseif ($parts['scheme'] == 'https') {
        $port = isset($parts['port']) ? $parts['port'] : 443;
        $host = 'ssl://' . $uri['host'];
    } else {
        $result['error-code'] = 1;
        $result['error'] = 'Unsupported protocol';
    }
    
    $fp = @fsockopen($host, $port, $errno, $errstr, 15);

    if (!$fp) {
        $result['error-code'] = 7;
        $result['error'] = "Cannot connect: Error $errno:" . trim($errstr);
        return $result;
    }
    
    if (isset($parts['path'])) {
        $path = $url_parts['path'];
        if (isset($parts['query'])) $path .= '?' . $url_parts['query'];
    } else {
        $path = '/';
    }

    $headers = array_merge(
        array(
            'Host' => $parts['host'],
            'User-Agent' => SIMPLEID_USER_AGENT,
            'Connection' => 'close'
        ),
        $headers
    );

    if (isset($parts['user']) && isset($parts['pass'])) {
        $headers['Authorization'] = 'Basic '. base64_encode($uri['user'] . (!empty($uri['pass']) ? ":". $uri['pass'] : ''));
    }
  
    $request = $method . ' '. $path ." HTTP/1.0\r\n";
    
    $keys = array_keys($headers);
    for ($i = 0; $i < count($keys); $i++) {
        $request .= $keys[$i] . ': ' . $headers[$keys[$i]] . "\r\n";
    }
    
    // End of headers - separator
    $request .= "\r\n";
    
    fwrite($fp, $request);

    // Fetch response.
    $response = '';
    while (!feof($fp) && $chunk = fread($fp, 1024)) {
        $response .= $chunk;
    }
    fclose($fp);

    // Parse response.
    list($header_block, $result['data']) = explode("\r\n\r\n", $response, 2);
    
    $result = array_merge($result, _http_parse_headers($header_block, FALSE));
    
    // Process redirects
    if (($result['code'] == 301) || ($result['code'] == 302) || ($result['code'] == 307)) {
        if ($retry == 0) {
            // Too many times, return an error
            $result['error-code'] = 47;
            $result['error'] = 'Too many redirects';
        } else {
            $result = _http_request_fsock($result['headers']['Location'], $headers, $method, $retry - 1);
        }
    }

    $result['url'] = $url;
    return $result;
}

/**
 * Parses HTTP response headers.
 *
 * @param string $header_block the unparsed header block
 * @param bool $curl if true, use simplified parsing as libcurl already parses
 * the headers
 * @return if $curl is true, an array of response headers, otherwise additional parsing
 * is done for 'code' and 'content-type'
 */
function _http_parse_headers($header_block, $curl) {
    $headers = array();
    $result = array();
    
    $lines = preg_split("/\r\n|\n|\r/", $header_block);
    $status = array_shift($lines);  // Get rid of the status
    
    if (!$curl) {
        // If we are not using CURL, we need to parse the status line as well
        list($protocol, $result['code'], $text) = explode(' ', trim($status), 3);
    }

    // Parse headers.
    while ($line = trim(array_shift($lines))) {
        list($header, $value) = explode(':', $line, 2);

        if (isset($headers[$header]) && $header == 'Set-Cookie') {
            // RFC 2109: the Set-Cookie response header comprises the token Set-
            // Cookie:, followed by a comma-separated list of one or more cookies.
            $headers[$header] .= ','. trim($value);
        } else {
            $headers[$header] = trim($value);
        }
        
        if (!$curl && (strtolower($header) == 'content-type')) $result['content-type'] = $value;
    }
        
    if ($curl) {  
        return $headers;
    } else {
        return $result;
    }
}
?>