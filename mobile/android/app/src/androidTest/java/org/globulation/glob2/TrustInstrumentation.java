package org.globulation.glob2;

import android.app.Activity;
import android.app.Instrumentation;
import android.os.Bundle;
import java.security.cert.Certificate;
import java.security.cert.CertificateFactory;
import javax.net.ssl.SSLParameters;
import javax.net.ssl.SSLSocket;
import javax.net.ssl.SSLSocketFactory;

/** Explicit developer test APK; never runs during ordinary game startup. */
public final class TrustInstrumentation extends Instrumentation {
    @Override public void onCreate(Bundle arguments) { super.onCreate(arguments); start(); }
    private static void require(boolean value, String message) {
        if (!value) throw new AssertionError(message);
    }
    @Override public void onStart() {
        Bundle result = new Bundle();
        try {
            // Public TLS handshake, with the platform's normal chain AND hostname checks.
            try (SSLSocket socket = (SSLSocket) SSLSocketFactory.getDefault().createSocket()) {
                socket.connect(new java.net.InetSocketAddress("github.com", 443), 10000);
                socket.setSoTimeout(10000);
                SSLParameters parameters = socket.getSSLParameters();
                parameters.setEndpointIdentificationAlgorithm("HTTPS");
                socket.setSSLParameters(parameters);
                socket.startHandshake();
                Certificate[] peer = socket.getSession().getPeerCertificates();
                byte[][] chain = new byte[peer.length][];
                for (int i = 0; i < peer.length; ++i) chain[i] = peer[i].getEncoded();
                require(Glob2Activity.verifyServerCertificates(chain, "github.com"), "Platform trust rejected the independently verified public chain");
                byte[] trailing = java.util.Arrays.copyOf(chain[0], chain[0].length + 1);
                require(!Glob2Activity.verifyServerCertificates(new byte[][] {trailing}, "github.com"), "Trailing certificate bytes accepted");
            }
            try (java.io.InputStream input = getContext().getAssets().open("untrusted.pem")) {
                byte[] unknown = CertificateFactory.getInstance("X.509").generateCertificate(input).getEncoded();
                require(!Glob2Activity.verifyServerCertificates(new byte[][] {unknown}, "mobile.test"), "Unknown issuer accepted");
            }
            require(!Glob2Activity.verifyServerCertificates(new byte[][] {{1,2,3}}, "github.com"), "Malformed DER accepted");
            require(!Glob2Activity.verifyServerCertificates(new byte[0][], "github.com"), "Empty chain accepted");
            require(!Glob2Activity.verifyServerCertificates(new byte[17][], "github.com"), "Oversized chain accepted");
            result.putString("glob2Trust", "PASS");
            finish(Activity.RESULT_OK, result);
        } catch (Throwable failure) {
            result.putString("glob2Trust", "FAIL: " + failure);
            finish(Activity.RESULT_CANCELED, result);
        }
    }
}
