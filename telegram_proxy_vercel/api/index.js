const https = require('https');

module.exports = async (req, res) => {
  // Set CORS headers
  res.setHeader('Access-Control-Allow-Origin', '*');
  res.setHeader('Access-Control-Allow-Methods', 'GET, POST, OPTIONS');
  res.setHeader('Access-Control-Allow-Headers', 'Content-Type');

  if (req.method === 'OPTIONS') {
    res.status(200).end();
    return;
  }

  // Extract the path after the domain
  const path = req.url;

  // Build the target URL
  const targetUrl = `https://api.telegram.org${path}`;
  console.log(`Forwarding request to: ${targetUrl}`);

  // Get request body
  let body = [];
  req.on('data', (chunk) => {
    body.push(chunk);
  }).on('end', () => {
    body = Buffer.concat(body);

    const headers = { ...req.headers };
    // Remove host header to avoid SSL/Routing issues on Telegram side
    delete headers.host;
    // Set connection to close to avoid socket leaks
    headers.connection = 'close';

    const options = {
      method: req.method,
      headers: headers,
      timeout: 10000
    };

    const targetReq = https.request(targetUrl, options, (targetRes) => {
      // Forward status code
      res.status(targetRes.statusCode || 200);

      // Forward response headers
      for (const [key, value] of Object.entries(targetRes.headers)) {
        res.setHeader(key, value);
      }

      // Pipe response
      targetRes.pipe(res);
    });

    targetReq.on('error', (err) => {
      console.error(`Proxy request error: ${err.message}`);
      res.status(500).json({ error: 'Proxy request failed', details: err.message });
    });

    // Write body if present
    if (body.length > 0) {
      targetReq.write(body);
    }
    targetReq.end();
  });
};
