using System.Security.Cryptography;
using System.Security.Cryptography.X509Certificates;

var outDir = args.Length > 0 ? args[0] : Path.Combine("..", "certs");
Directory.CreateDirectory(outDir);

using var rsa = RSA.Create(2048);
var req = new CertificateRequest("CN=localhost", rsa, HashAlgorithmName.SHA256, RSASignaturePadding.Pkcs1);
req.CertificateExtensions.Add(new X509BasicConstraintsExtension(false, false, 0, false));
req.CertificateExtensions.Add(new X509KeyUsageExtension(X509KeyUsageFlags.DigitalSignature | X509KeyUsageFlags.KeyEncipherment, true));
req.CertificateExtensions.Add(new X509SubjectKeyIdentifierExtension(req.PublicKey, false));
var san = new SubjectAlternativeNameBuilder();
san.AddDnsName("localhost");
san.AddIpAddress(System.Net.IPAddress.Loopback);
san.AddIpAddress(System.Net.IPAddress.IPv6Loopback);
req.CertificateExtensions.Add(san.Build());

using var cert = req.CreateSelfSigned(DateTimeOffset.UtcNow.AddDays(-1), DateTimeOffset.UtcNow.AddYears(5));
var certPem = PemEncoding.Write("CERTIFICATE", cert.RawData);
var keyPem = PemEncoding.Write("PRIVATE KEY", rsa.ExportPkcs8PrivateKey());

var crt = Path.Combine(outDir, "server.crt");
var key = Path.Combine(outDir, "server.key");
File.WriteAllText(crt, new string(certPem) + "\n");
File.WriteAllText(key, new string(keyPem) + "\n");
Console.WriteLine("wrote " + Path.GetFullPath(crt));
Console.WriteLine("wrote " + Path.GetFullPath(key));
